#include "droras_client.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_state.h"
#include "heat_sources.h"
#include "heatboard/socketio_codec.h"

namespace {

const char* TAG = "droras";

constexpr int kWsOpcodeText = 0x1;
constexpr int kSendTimeoutMs = 1000;
constexpr int64_t kReconnectDelayMs = 3000;

// The heat_list frame the server sends on connect (and after a reload) is the largest: a
// few kilobytes for a race day. Reject larger frames rather than parsing a partial one.
constexpr std::size_t kMaxFrameBytes = 64 * 1024;

// How long a heat change sent from here may wait for its broadcast before it is written
// off (the connection probably dropped in between).
constexpr int64_t kHeatEchoTimeoutMs = 2000;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

std::optional<heatboard::ServerHeartbeatTiming> readHeartbeatTiming(const std::string& frame) {
  const char* end = nullptr;
  // The supplied length includes the string's final NUL. Reject partial JSON,
  // trailing bytes and embedded NULs rather than accepting a malformed handshake.
  cJSON* document = cJSON_ParseWithLengthOpts(frame.c_str() + 1, frame.size(), &end, true);
  std::optional<heatboard::ServerHeartbeatTiming> timing;
  if (cJSON_IsObject(document) && end == frame.c_str() + frame.size()) {
    const cJSON* interval = cJSON_GetObjectItemCaseSensitive(document, "pingInterval");
    const cJSON* timeout = cJSON_GetObjectItemCaseSensitive(document, "pingTimeout");
    if (cJSON_IsNumber(interval) && cJSON_IsNumber(timeout)) {
      timing = heatboard::serverHeartbeatTiming(interval->valuedouble, timeout->valuedouble);
    }
  }
  cJSON_Delete(document);
  return timing;
}

}  // namespace

DrorasClient& DrorasClient::instance() {
  static DrorasClient client;
  return client;
}

void DrorasClient::configure(const std::optional<heatboard::ServerAddress>& address) {
  configuredAddress_ = address;
  configurationChanged_ = true;
  retryAtMs_ = 0;
  std::lock_guard<std::mutex> lock(stateMutex_);
  // Invalidate callbacks immediately; poll stops the old client outside all locks.
  eventClient_ = nullptr;
  heartbeat_.reset();
  ready_ = false;
  failureSeen_ = false;
  restartRequested_ = false;
  pendingHeatEchoes_ = 0;
  frameBuffer_.clear();
  addressText_ = address ? heatboard::formatServerAddress(*address) : "";
  publishStatusLocked();
}

void DrorasClient::stopClient() {
  esp_websocket_client_handle_t previous = nullptr;
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    previous = client_;
    client_ = nullptr;
  }
  // Stopped outside the lock: stop waits for the websocket task, which may itself be
  // waiting to report a state change.
  if (previous != nullptr) {
    esp_websocket_client_stop(previous);
    esp_websocket_client_destroy(previous);
  }
}

void DrorasClient::startClient() {
  if (!configuredAddress_) return;

  // Socket.IO over a plain WebSocket, skipping the HTTP long-polling handshake — the same
  // transport the droras web UI uses.
  const std::string uri = "ws://" + heatboard::formatServerAddress(*configuredAddress_) +
                          "/socket.io/?EIO=4&transport=websocket";
  esp_websocket_client_config_t config = {};
  config.uri = uri.c_str();
  config.reconnect_timeout_ms = 3000;
  config.enable_close_reconnect = true;
  config.network_timeout_ms = 5000;
  // Transport liveness supplements Engine.IO monitoring. WebSocket PONGs alone
  // never extend the application's heartbeat deadline.
  config.ping_interval_sec = 5;
  config.pingpong_timeout_sec = 10;
  config.buffer_size = 2048;
  // The heat list is parsed on this task (cJSON plus the row conversion).
  config.task_stack = 8192;

  esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(TAG, "esp_websocket_client_init failed");
    std::lock_guard<std::mutex> lock(stateMutex_);
    failLocked();
    return;
  }
  if (esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, &DrorasClient::onEvent, this) != ESP_OK) {
    esp_websocket_client_destroy(client);
    std::lock_guard<std::mutex> lock(stateMutex_);
    failLocked();
    return;
  }
  {
    std::lock_guard<std::mutex> lock(clientMutex_);
    client_ = client;
  }
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    eventClient_ = client;
    heartbeat_.begin(nowMs());
    restartRequested_ = false;
    frameBuffer_.clear();
    publishStatusLocked();
  }
  const esp_err_t err = esp_websocket_client_start(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
    markUnresponsive(client);
    return;
  }
  ESP_LOGI(TAG, "connecting to %s", uri.c_str());
}

bool DrorasClient::isReady() const {
  if (!ready_) return false;
  std::lock_guard<std::mutex> lock(stateMutex_);
  return wifiAvailable_ && !restartRequested_ && heartbeat_.isHealthy(nowMs());
}

void DrorasClient::publishStatusLocked() {
  const RpiStatus status = addressText_.empty() ? RpiStatus::Disabled
      : ready_ && wifiAvailable_ && heartbeat_.isHealthy(nowMs()) ? RpiStatus::Connected
      : failureSeen_ ? RpiStatus::Unresponsive : RpiStatus::Connecting;
  if (publishedStatus_ == static_cast<int>(status) && publishedAddress_ == addressText_) return;
  publishedStatus_ = static_cast<int>(status);
  publishedAddress_ = addressText_;
  AppState::instance().setRpi(status, addressText_);
}

void DrorasClient::failLocked() {
  ready_ = false;
  heartbeat_.fail();
  failureSeen_ = true;
  restartRequested_ = true;
  pendingHeatEchoes_ = 0;
  publishStatusLocked();
}

void DrorasClient::markUnresponsive(esp_websocket_client_handle_t client) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  if (eventClient_ != client) return;
  failLocked();
}

void DrorasClient::poll(bool wifiConnected) {
  if (configurationChanged_) {
    stopClient();
    configurationChanged_ = false;
  }
  bool shouldStop = false;
  bool shouldStart = false;
  const int64_t now = nowMs();
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!configuredAddress_) {
      wifiAvailable_ = wifiConnected;
      return;
    }
    if (!wifiConnected) {
      if (wifiAvailable_) failLocked();
      wifiAvailable_ = false;
      eventClient_ = nullptr;
      heartbeat_.reset();
      ready_ = false;
      restartRequested_ = false;
      pendingHeatEchoes_ = 0;
      retryAtMs_ = 0;
      frameBuffer_.clear();
      shouldStop = true;
    } else {
      wifiAvailable_ = true;
      if (heartbeat_.poll(now)) {
        ESP_LOGW(TAG, "server handshake or heartbeat timed out");
        failLocked();
      }
      if (restartRequested_) {
        eventClient_ = nullptr;
        heartbeat_.reset();
        restartRequested_ = false;
        frameBuffer_.clear();
        retryAtMs_ = now + kReconnectDelayMs;
        shouldStop = true;
      } else if (eventClient_ == nullptr && now >= retryAtMs_) {
        shouldStart = true;
      }
    }
    publishStatusLocked();
  }
  if (shouldStop) stopClient();
  if (shouldStart) startClient();
}

void DrorasClient::onEvent(void* arg, esp_event_base_t, int32_t eventId, void* eventData) {
  auto* self = static_cast<DrorasClient*>(arg);
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);
  if (data == nullptr) return;
  std::string completeFrame;
  {
    std::lock_guard<std::mutex> lock(self->stateMutex_);
    if (self->eventClient_ != data->client || self->restartRequested_) return;
    switch (eventId) {
      case WEBSOCKET_EVENT_CONNECTED:
        self->heartbeat_.begin(nowMs());
        self->ready_ = false;
        self->frameBuffer_.clear();
        self->publishStatusLocked();
        break;

      case WEBSOCKET_EVENT_DISCONNECTED:
      case WEBSOCKET_EVENT_CLOSED:
      case WEBSOCKET_EVENT_ERROR:
        self->failLocked();
        break;

      case WEBSOCKET_EVENT_DATA: {
        if (data->op_code != kWsOpcodeText) break;
        if (data->data_len < 0 || data->payload_offset < 0 || data->payload_len < 0 ||
            static_cast<std::size_t>(data->payload_len) > kMaxFrameBytes) {
          self->failLocked();
          break;
        }
        // One WebSocket frame can arrive split over several events.
        if (data->payload_offset == 0) self->frameBuffer_.clear();
        if (self->frameBuffer_.size() != static_cast<std::size_t>(data->payload_offset) ||
            static_cast<std::size_t>(data->data_len) > kMaxFrameBytes - self->frameBuffer_.size() ||
            (data->data_len > 0 && data->data_ptr == nullptr)) {
          self->failLocked();
          break;
        }
        if (data->data_len > 0) self->frameBuffer_.append(data->data_ptr, data->data_len);
        if (self->frameBuffer_.size() >= static_cast<std::size_t>(data->payload_len)) {
          completeFrame.swap(self->frameBuffer_);
        }
        break;
      }

      default:
        break;
    }
  }
  if (!completeFrame.empty()) self->onFrame(data->client, completeFrame);
}

void DrorasClient::onFrame(esp_websocket_client_handle_t client, const std::string& frame) {
  using heatboard::SioPacketType;
  const heatboard::SioPacket packet = heatboard::decodeSioPacket(frame);

  std::string reply;
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (eventClient_ != client || restartRequested_) return;
    switch (packet.type) {
      case SioPacketType::EngineOpen: {
        const auto timing = readHeartbeatTiming(frame);
        if (!timing || !heartbeat_.engineOpened(*timing, nowMs())) {
          failLocked();
          break;
        }
        reply = heatboard::kSioNamespaceConnect;
        break;
      }

      case SioPacketType::EnginePing:
        if (!heartbeat_.receivedPing(nowMs())) {
          failLocked();
          break;
        }
        reply = "3" + frame.substr(1);
        break;

      case SioPacketType::NamespaceConnected:
        if (!heartbeat_.namespaceJoined(nowMs())) {
          failLocked();
          break;
        }
        ESP_LOGI(TAG, "server namespace joined; heartbeat monitoring active");
        pendingHeatEchoes_ = 0;
        ready_ = true;
        failureSeen_ = false;
        publishStatusLocked();
        break;

      case SioPacketType::NamespaceDisconnected:
      case SioPacketType::ConnectError:
      case SioPacketType::EngineClose:
        failLocked();
        break;

      case SioPacketType::Event:
        // A server's connect handler may emit initial data before its namespace ACK.
        // Accept that data without treating it as a heartbeat or command readiness.
        if (packet.eventName == "current_heat") {
          if (const auto heat = heatboard::parseSioIntArg(packet.argsJson)) {
            ESP_LOGI(TAG, "current heat: %d", *heat);
            // The server broadcasts every change in order, including the ones sent from here,
            // which AppState already shows. While several of those are still on their way,
            // the earlier broadcasts are stale: applying them would move the display back
            // (two quick taps on "next" would show N+1, N+2, N+1, N+2). Only the broadcast
            // that settles the last change sent is applied.
            const bool awaitingEchoes = nowMs() - lastHeatCommandMs_ < kHeatEchoTimeoutMs;
            if (!awaitingEchoes) {
              pendingHeatEchoes_ = 0;
            } else if (pendingHeatEchoes_ > 0 && --pendingHeatEchoes_ > 0) {
              break;
            }
            AppState::instance().setCurrentHeat(*heat, HeatSource::Rpi);
          } else {
            ESP_LOGW(TAG, "current_heat with unexpected payload: %s", packet.argsJson.c_str());
          }
        } else if (packet.eventName == "heat_list") {
          // The server's own copy of the sheet. It is applied whenever it arrives: at a venue
          // without internet access this is the only way to get the pilot names, and it is
          // what the operator's web UI shows, so it is never older than what we could fetch.
          heat_sources::HeatListResult list = heat_sources::parseDrorasHeatList(packet.argsJson);
          if (list.heats) {
            AppState::instance().setHeatList(std::move(list.heats), HeatListSource::Rpi);
          } else {
            AppState::instance().setHeatListFailed(std::move(list.error));
          }
        }
        break;

      default:
        break;
    }
  }
  // No application lock is held: send failures can synchronously invoke onEvent.
  // configure() invalidates this handle first, then waits for this callback before
  // destroying it; a callback never takes clientMutex_.
  if (!reply.empty()) {
    const int sent = esp_websocket_client_send_text(client, reply.data(), reply.size(), pdMS_TO_TICKS(kSendTimeoutMs));
    if (sent != static_cast<int>(reply.size())) markUnresponsive(client);
  }
}

bool DrorasClient::sendCommand(const std::string& frame) {
  std::lock_guard<std::mutex> lock(clientMutex_);
  if (client_ == nullptr || !isReady()) {
    return false;
  }
  const int sent = esp_websocket_client_send_text(client_, frame.data(), frame.size(), pdMS_TO_TICKS(kSendTimeoutMs));
  if (sent != static_cast<int>(frame.size())) {
    ESP_LOGW(TAG, "server command send failed");
    markUnresponsive(client_);
    return false;
  }
  return true;
}

bool DrorasClient::setCurrentHeat(int heatNumber) {
  // Counted before sending: the broadcast must never be able to overtake the bookkeeping.
  ++pendingHeatEchoes_;
  lastHeatCommandMs_ = nowMs();
  if (!sendCommand(heatboard::encodeSioEvent("set_current_heat", heatNumber))) {
    pendingHeatEchoes_ = 0;
    return false;
  }
  AppState::instance().setCurrentHeat(heatNumber, HeatSource::Rpi);
  return true;
}

bool DrorasClient::startHeat() { return sendCommand(heatboard::encodeSioEvent("start_heat")); }

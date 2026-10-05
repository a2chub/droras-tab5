#include "droras_client.h"

#include <algorithm>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_state.h"
#include "heatboard/socketio_codec.h"

namespace {

const char* TAG = "droras";

constexpr int kWsOpcodeText = 0x1;
constexpr int kSendTimeoutMs = 1000;

// Only the short control and current_heat frames matter here. The heat_list frame the
// server sends on connect is several kilobytes and unused (the pilot list comes from the
// Apps Script endpoint), so anything longer is cut off and falls through as unknown.
constexpr std::size_t kMaxFrameBytes = 256;

// How long a heat change sent from here may wait for its broadcast before it is written
// off (the connection probably dropped in between).
constexpr int64_t kHeatEchoTimeoutMs = 2000;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

}  // namespace

DrorasClient& DrorasClient::instance() {
  static DrorasClient client;
  return client;
}

void DrorasClient::configure(const std::optional<heatboard::ServerAddress>& address) {
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
  ready_ = false;

  if (!address) {
    addressText_.clear();
    AppState::instance().setRpi(RpiStatus::Disabled, "");
    ESP_LOGI(TAG, "no server configured");
    return;
  }

  addressText_ = heatboard::formatServerAddress(*address);
  AppState::instance().setRpi(RpiStatus::Connecting, addressText_);

  // Socket.IO over a plain WebSocket, skipping the HTTP long-polling handshake — the same
  // transport the droras web UI uses.
  const std::string uri = "ws://" + addressText_ + "/socket.io/?EIO=4&transport=websocket";
  esp_websocket_client_config_t config = {};
  config.uri = uri.c_str();
  config.reconnect_timeout_ms = 3000;
  config.network_timeout_ms = 5000;
  config.buffer_size = 2048;
  config.task_stack = 6144;

  esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
  if (client == nullptr) {
    ESP_LOGE(TAG, "esp_websocket_client_init failed");
    return;
  }
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, &DrorasClient::onEvent, this);
  const esp_err_t err = esp_websocket_client_start(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_websocket_client_start failed: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(client);
    return;
  }

  std::lock_guard<std::mutex> lock(clientMutex_);
  client_ = client;
  ESP_LOGI(TAG, "connecting to %s", uri.c_str());
}

void DrorasClient::markDisconnected() {
  pendingHeatEchoes_ = 0;
  if (ready_.exchange(false)) {
    ESP_LOGW(TAG, "connection to %s lost", addressText_.c_str());
  }
  AppState::instance().setRpi(RpiStatus::Connecting, addressText_);
}

void DrorasClient::onEvent(void* arg, esp_event_base_t, int32_t eventId, void* eventData) {
  auto* self = static_cast<DrorasClient*>(arg);
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);

  switch (eventId) {
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
      self->markDisconnected();
      break;

    case WEBSOCKET_EVENT_DATA: {
      if (data->op_code != kWsOpcodeText) {
        break;
      }
      // One WebSocket frame can arrive split over several events.
      if (data->payload_offset == 0) {
        self->frameBuffer_.clear();
      }
      const std::size_t room = kMaxFrameBytes - std::min(kMaxFrameBytes, self->frameBuffer_.size());
      self->frameBuffer_.append(data->data_ptr, std::min<std::size_t>(room, data->data_len));
      if (data->payload_offset + data->data_len >= data->payload_len) {
        self->onFrame(data->client, self->frameBuffer_);
      }
      break;
    }

    default:
      break;
  }
}

void DrorasClient::onFrame(esp_websocket_client_handle_t client, const std::string& frame) {
  using heatboard::SioPacketType;
  const heatboard::SioPacket packet = heatboard::decodeSioPacket(frame);

  // Replies go through the handle from the event rather than sendCommand(): this runs on
  // the websocket task, and taking clientMutex_ here would deadlock against configure().
  switch (packet.type) {
    case SioPacketType::EngineOpen:
      esp_websocket_client_send_text(client, heatboard::kSioNamespaceConnect.data(),
                                     heatboard::kSioNamespaceConnect.size(), pdMS_TO_TICKS(kSendTimeoutMs));
      break;

    case SioPacketType::EnginePing:
      // The server drops clients that stop answering its heartbeat.
      esp_websocket_client_send_text(client, heatboard::kSioEnginePong.data(), heatboard::kSioEnginePong.size(),
                                     pdMS_TO_TICKS(kSendTimeoutMs));
      break;

    case SioPacketType::NamespaceConnected:
      ESP_LOGI(TAG, "connected to %s", addressText_.c_str());
      pendingHeatEchoes_ = 0;
      ready_ = true;
      AppState::instance().setRpi(RpiStatus::Connected, addressText_);
      break;

    case SioPacketType::NamespaceDisconnected:
    case SioPacketType::ConnectError:
    case SioPacketType::EngineClose:
      markDisconnected();
      break;

    case SioPacketType::Event:
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
      }
      break;

    default:
      break;
  }
}

bool DrorasClient::sendCommand(const std::string& frame) {
  std::lock_guard<std::mutex> lock(clientMutex_);
  if (client_ == nullptr || !ready_) {
    return false;
  }
  const int sent = esp_websocket_client_send_text(client_, frame.data(), frame.size(), pdMS_TO_TICKS(kSendTimeoutMs));
  if (sent < 0) {
    ESP_LOGW(TAG, "send failed: %s", frame.c_str());
    return false;
  }
  return true;
}

bool DrorasClient::setCurrentHeat(int heatNumber) {
  // Counted before sending: the broadcast must never be able to overtake the bookkeeping.
  ++pendingHeatEchoes_;
  lastHeatCommandMs_ = nowMs();
  if (!sendCommand(heatboard::encodeSioEvent("set_current_heat", heatNumber))) {
    --pendingHeatEchoes_;
    return false;
  }
  AppState::instance().setCurrentHeat(heatNumber, HeatSource::Rpi);
  return true;
}

bool DrorasClient::startHeat() { return sendCommand(heatboard::encodeSioEvent("start_heat")); }

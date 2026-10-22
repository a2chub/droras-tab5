#include "wifi_station.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app_state.h"
#include "settings_store.h"

// Existing private presets remain optional defaults. A clean checkout can be built and
// configured entirely on the device, with no secrets header.
#if __has_include("app_secrets.h") && !defined(HEATBOARD_WIFI_NO_BUILD_PRESETS)
#include "app_secrets.h"
#else
#include "app_secrets.example.h"
#endif

namespace wifi_station {
namespace {

const char* TAG = "wifi";
constexpr int64_t kReconnectDelayUs = 2'000'000;
constexpr int64_t kConnectionTimeoutUs = 30'000'000;
constexpr int64_t kSignalPollIntervalUs = 2'000'000;

struct Preset {
  const char* label;
  const char* ssid;
  const char* password;
};
constexpr Preset kDefaults[kNetworkSlots] = {
    {APP_WIFI_1_LABEL, APP_WIFI_1_SSID, APP_WIFI_1_PASSWORD},
    {APP_WIFI_2_LABEL, APP_WIFI_2_SSID, APP_WIFI_2_PASSWORD},
    {APP_WIFI_3_LABEL, APP_WIFI_3_SSID, APP_WIFI_3_PASSWORD},
    {APP_WIFI_4_LABEL, APP_WIFI_4_SSID, APP_WIFI_4_PASSWORD},
};

std::mutex profilesMutex;
std::array<heatboard::WifiProfile, kNetworkSlots> profiles;
std::array<bool, kNetworkSlots> storedProfiles{};
std::atomic<bool> connected{false};
std::atomic<bool> switching{false};
std::atomic<bool> stopped{false};
std::atomic<int64_t> reconnectAtUs{0};
std::atomic<int64_t> connectingSinceUs{0};
bool netifReady = false;
bool hostedReady = false;
bool driverReady = false;
bool wifiHandlerReady = false;
bool ipHandlerReady = false;
bool radioStarted = false;
std::atomic<bool> failureReported{false};
bool waitingForStop = false;
int64_t stopRequestedUs = 0;
int64_t nextSignalPollUs = 0;
heatboard::WifiProfile pendingNetwork;

bool validSlot(int slot) { return slot >= 0 && slot < kNetworkSlots; }

void onWifiEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  if (eventId == WIFI_EVENT_STA_STOP) {
    stopped = true;
    return;
  }
  if (switching) return;
  if (eventId == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
    ESP_LOGW(TAG, "disconnected (reason %d), retrying", event->reason);
    connected = false;
    const int64_t now = esp_timer_get_time();
    int64_t unset = 0;
    connectingSinceUs.compare_exchange_strong(unset, now);
    const bool timedOut = now - connectingSinceUs.load() >= kConnectionTimeoutUs;
    AppState::instance().setWifi(timedOut ? WifiStatus::Failed : WifiStatus::Connecting, "");
    reconnectAtUs = now + kReconnectDelayUs;
  }
}

void onGotIp(void*, esp_event_base_t, int32_t, void* eventData) {
  if (switching) return;
  const auto* event = static_cast<ip_event_got_ip_t*>(eventData);
  char ip[16];
  snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
  ESP_LOGI(TAG, "connected, ip=%s", ip);
  reconnectAtUs = 0;
  connectingSinceUs = 0;
  connected = true;
  failureReported = false;
  AppState::instance().setWifi(WifiStatus::Connected, ip);
}

esp_err_t applyNetwork(const heatboard::WifiProfile& network) {
  wifi_config_t config = {};
  // These fields may occupy every byte: SSID=32 bytes and raw hex PSK=64 bytes.
  // Validation happens before this call; strlcpy would silently truncate those cases.
  memcpy(config.sta.ssid, network.ssid.data(), network.ssid.size());
  memcpy(config.sta.password, network.password.data(), network.password.size());
  return esp_wifi_set_config(WIFI_IF_STA, &config);
}

esp_err_t initializeRadio() {
  // Track each step so a failed initialization can be retried from the settings screen.
  esp_err_t err = ESP_OK;
  if (!netifReady) {
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;
    netifReady = true;
  }
  if (!driverReady) {
    // PlatformIO drops the object containing esp_hosted's static constructor.
    if (!hostedReady) {
      err = esp_hosted_init();
      if (err != ESP_OK) return err;
      hostedReady = true;
    }
    wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&initConfig);
    if (err != ESP_OK) return err;
    driverReady = true;
  }
  if (!wifiHandlerReady) {
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr, nullptr);
    if (err != ESP_OK) return err;
    wifiHandlerReady = true;
  }
  if (!ipHandlerReady) {
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onGotIp, nullptr, nullptr);
    if (err != ESP_OK) return err;
    ipHandlerReady = true;
  }
  // Credentials are owned by settings_store, not a second copy in the Wi-Fi driver NVS.
  err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (err != ESP_OK) return err;
  return esp_wifi_set_mode(WIFI_MODE_STA);
}

void connectionFailed(esp_err_t err) {
  // Ignore queued events until another explicit attempt starts a configured station.
  switching = true;
  connected = false;
  reconnectAtUs = 0;
  connectingSinceUs = 0;
  ESP_LOGE(TAG, "applying Wi-Fi configuration failed: %s", esp_err_to_name(err));
  AppState::instance().setWifi(WifiStatus::Failed, "");
}

void startPendingNetwork() {
  esp_err_t err = initializeRadio();
  if (err != ESP_OK) return connectionFailed(err);
  err = applyNetwork(pendingNetwork);
  if (err != ESP_OK) return connectionFailed(err);
  err = esp_wifi_start();
  if (err != ESP_OK) return connectionFailed(err);
  radioStarted = true;
  connectingSinceUs = esp_timer_get_time();
  reconnectAtUs = connectingSinceUs.load();
  switching = false;
}

}  // namespace

heatboard::WifiProfile profile(int slot) {
  std::lock_guard<std::mutex> lock(profilesMutex);
  return validSlot(slot) ? profiles[slot] : heatboard::WifiProfile{};
}

std::string ssid(int slot) { return profile(slot).ssid; }

std::string label(int slot) {
  std::lock_guard<std::mutex> lock(profilesMutex);
  if (!validSlot(slot)) return "";
  if (!storedProfiles[slot] && !profiles[slot].ssid.empty() && kDefaults[slot].label[0] != '\0') {
    return kDefaults[slot].label;
  }
  return profiles[slot].ssid;
}

esp_err_t saveProfile(int slot, const heatboard::WifiProfile& network) {
  if (!validSlot(slot) || heatboard::validateWifiProfile(network) != heatboard::WifiProfileError::None) {
    return ESP_ERR_INVALID_ARG;
  }
  std::lock_guard<std::mutex> lock(profilesMutex);
  const esp_err_t err = settings_store::saveWifiProfile(slot, network);
  if (err == ESP_OK) {
    profiles[slot] = network;
    storedProfiles[slot] = true;
  }
  return err;
}

void start() {
  {
    std::lock_guard<std::mutex> lock(profilesMutex);
    for (int i = 0; i < kNetworkSlots; ++i) {
      const auto saved = settings_store::loadWifiProfile(i);
      storedProfiles[i] = saved.has_value();
      profiles[i] = saved.value_or(heatboard::WifiProfile{kDefaults[i].ssid, kDefaults[i].password});
      if (heatboard::validateWifiProfile(profiles[i]) != heatboard::WifiProfileError::None) {
        profiles[i] = {};
      }
    }
  }
  int slot = settings_store::loadWifiSlot().value_or(-1);
  if (profile(slot).ssid.empty()) {
    slot = -1;
    for (int i = 0; i < kNetworkSlots; ++i) {
      if (!profile(i).ssid.empty()) {
        slot = i;
        break;
      }
    }
  }
  if (slot < 0) {
    AppState::instance().setWifiSlot(-1);
    AppState::instance().setWifi(WifiStatus::NotConfigured, "");
    return;
  }
  switchTo(slot);
}

void switchTo(int slot) {
  const auto network = profile(slot);
  if (!validSlot(slot) || heatboard::validateWifiProfile(network) != heatboard::WifiProfileError::None) return;

  // Commit the selection before disrupting the current connection, and report any error.
  if (settings_store::loadWifiSlot() != slot) {
    const esp_err_t err = settings_store::saveWifiSlot(slot);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "saving Wi-Fi selection failed: %s", esp_err_to_name(err));
      AppState::instance().setWifi(WifiStatus::Failed, "");
      return;
    }
  }

  switching = true;
  connected = false;
  reconnectAtUs = 0;
  connectingSinceUs = 0;
  failureReported = false;
  AppState::instance().setWifiSlot(slot);
  AppState::instance().setWifi(WifiStatus::Connecting, "");
  pendingNetwork = network;
  if (waitingForStop) return;

  // Stop cancels a connection attempt as well as an established connection. Re-applying
  // the current slot must work after its password has been edited.
  if (radioStarted) {
    stopped = false;
    const esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) return connectionFailed(err);
    radioStarted = false;
    // Wait for the event loop to consume the old station's final event. During this
    // interval old DISCONNECTED/GOT_IP events cannot overwrite the new connection.
    waitingForStop = true;
    stopRequestedUs = esp_timer_get_time();
    return;
  }
  startPendingNetwork();
}

void poll() {
  const int64_t now = esp_timer_get_time();
  if (waitingForStop) {
    if (stopped) {
      waitingForStop = false;
      startPendingNetwork();
    } else if (esp_timer_get_time() - stopRequestedUs >= kConnectionTimeoutUs && !failureReported) {
      AppState::instance().setWifi(WifiStatus::Failed, "");
      failureReported = true;
    }
    return;
  }
  if (connected) {
    failureReported = false;
    if (now >= nextSignalPollUs) {
      nextSignalPollUs = now + kSignalPollIntervalUs;
      wifi_ap_record_t accessPoint{};
      const esp_err_t err = esp_wifi_sta_get_ap_info(&accessPoint);
      AppState::instance().setWifiRssi(err == ESP_OK ? std::optional<int>(accessPoint.rssi) : std::nullopt);
    }
    return;
  }
  nextSignalPollUs = 0;
  if (!radioStarted || switching) return;
  const int64_t since = connectingSinceUs.load();
  if (since != 0 && now - since >= kConnectionTimeoutUs && !failureReported) {
    AppState::instance().setWifi(WifiStatus::Failed, "");
    failureReported = true;
  }
  const int64_t due = reconnectAtUs.load();
  if (due == 0 || now < due) return;
  reconnectAtUs = 0;
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    reconnectAtUs = now + kReconnectDelayUs;
  }
}

bool isConnected() { return connected; }

}  // namespace wifi_station

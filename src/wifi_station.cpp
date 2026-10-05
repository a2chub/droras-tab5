#include "wifi_station.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "app_state.h"
#include "settings_store.h"

#if __has_include("app_secrets.h")
#include "app_secrets.h"
#else
#error "src/app_secrets.h is missing: copy src/app_secrets.example.h to src/app_secrets.h and fill in the Wi-Fi networks"
#endif

namespace wifi_station {

namespace {

const char* TAG = "wifi";
constexpr uint64_t kReconnectDelayUs = 2'000'000;
constexpr uint64_t kSwitchDelayUs = 200'000;

struct Network {
  const char* label;
  const char* ssid;
  const char* password;
};

constexpr Network kNetworks[kNetworkSlots] = {
    {APP_WIFI_1_LABEL, APP_WIFI_1_SSID, APP_WIFI_1_PASSWORD},
    {APP_WIFI_2_LABEL, APP_WIFI_2_SSID, APP_WIFI_2_PASSWORD},
    {APP_WIFI_3_LABEL, APP_WIFI_3_SSID, APP_WIFI_3_PASSWORD},
    {APP_WIFI_4_LABEL, APP_WIFI_4_SSID, APP_WIFI_4_PASSWORD},
};

std::atomic<bool> connected{false};
bool radioReady = false;
int activeSlot = -1;
esp_timer_handle_t reconnectTimer = nullptr;

bool isUsable(int slot) { return slot >= 0 && slot < kNetworkSlots && kNetworks[slot].ssid[0] != '\0'; }

void connect(void*) {
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
  }
}

void scheduleConnect(uint64_t delayUs) {
  esp_timer_stop(reconnectTimer);
  esp_timer_start_once(reconnectTimer, delayUs);
}

void onWifiEvent(void*, esp_event_base_t, int32_t eventId, void* eventData) {
  if (eventId == WIFI_EVENT_STA_START) {
    connect(nullptr);
  } else if (eventId == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* event = static_cast<wifi_event_sta_disconnected_t*>(eventData);
    ESP_LOGW(TAG, "disconnected (reason %d), retrying", event->reason);
    connected = false;
    AppState::instance().setWifi(WifiStatus::Connecting, "");
    // Retrying immediately spins when the access point is absent or rejects the password.
    scheduleConnect(kReconnectDelayUs);
  }
}

void onGotIp(void*, esp_event_base_t, int32_t, void* eventData) {
  const auto* event = static_cast<ip_event_got_ip_t*>(eventData);
  char ip[16];
  snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
  ESP_LOGI(TAG, "connected, ip=%s", ip);
  connected = true;
  AppState::instance().setWifi(WifiStatus::Connected, ip);
}

esp_err_t applyNetwork(int slot) {
  wifi_config_t config = {};
  strlcpy(reinterpret_cast<char*>(config.sta.ssid), kNetworks[slot].ssid, sizeof(config.sta.ssid));
  strlcpy(reinterpret_cast<char*>(config.sta.password), kNetworks[slot].password, sizeof(config.sta.password));
  return esp_wifi_set_config(WIFI_IF_STA, &config);
}

esp_err_t bringUp(int slot) {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) return err;
  err = esp_event_loop_create_default();
  // M5Unified or another component may already have created the default loop.
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
  if (esp_netif_create_default_wifi_sta() == nullptr) return ESP_FAIL;

  const esp_timer_create_args_t timerArgs = {
      .callback = &connect,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "wifi_reconnect",
      .skip_unhandled_events = true,
  };
  err = esp_timer_create(&timerArgs, &reconnectTimer);
  if (err != ESP_OK) return err;

  // esp_hosted normally initialises itself from a static constructor, but that object file
  // is not referenced by anything and the PlatformIO link drops it ("Transport not
  // initialized" at esp_wifi_init). The call is idempotent, so make it explicit.
  if (esp_hosted_init() != ESP_OK) return ESP_FAIL;

  // On the P4 this is where esp_hosted resets the C6 and opens the SDIO link.
  wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&initConfig);
  if (err != ESP_OK) return err;

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr, nullptr);
  if (err != ESP_OK) return err;
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onGotIp, nullptr, nullptr);
  if (err != ESP_OK) return err;

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) return err;
  err = applyNetwork(slot);
  if (err != ESP_OK) return err;
  return esp_wifi_start();
}

}  // namespace

const char* ssid(int slot) { return slot >= 0 && slot < kNetworkSlots ? kNetworks[slot].ssid : ""; }

const char* label(int slot) {
  if (slot < 0 || slot >= kNetworkSlots) {
    return "";
  }
  return kNetworks[slot].label[0] != '\0' ? kNetworks[slot].label : kNetworks[slot].ssid;
}

void start() {
  int slot = settings_store::loadWifiSlot().value_or(-1);
  // The remembered slot may have been emptied in a later build; fall back to the first filled one.
  for (int candidate = 0; !isUsable(slot) && candidate < kNetworkSlots; ++candidate) {
    if (isUsable(candidate)) {
      slot = candidate;
    }
  }
  if (!isUsable(slot)) {
    ESP_LOGW(TAG, "no network configured in app_secrets.h");
    AppState::instance().setWifi(WifiStatus::NotConfigured, "");
    return;
  }

  activeSlot = slot;
  AppState::instance().setWifiSlot(slot);
  AppState::instance().setWifi(WifiStatus::Connecting, "");
  ESP_LOGI(TAG, "using preset %d (%s)", slot + 1, kNetworks[slot].ssid);
  const esp_err_t err = bringUp(slot);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Wi-Fi bring-up failed: %s", esp_err_to_name(err));
    AppState::instance().setWifi(WifiStatus::Failed, "");
    return;
  }
  radioReady = true;
}

void switchTo(int slot) {
  if (!radioReady || !isUsable(slot) || slot == activeSlot) {
    return;
  }
  ESP_LOGI(TAG, "switching to preset %d (%s)", slot + 1, kNetworks[slot].ssid);

  // Disconnect first: the driver refuses a new configuration while a connection attempt
  // is in flight.
  esp_timer_stop(reconnectTimer);
  esp_wifi_disconnect();
  const esp_err_t err = applyNetwork(slot);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "applying preset %d failed: %s", slot + 1, esp_err_to_name(err));
    // Keep retrying the previous network rather than leaving the board offline.
    applyNetwork(activeSlot);
    scheduleConnect(kSwitchDelayUs);
    return;
  }

  activeSlot = slot;
  connected = false;
  settings_store::saveWifiSlot(slot);
  AppState::instance().setWifiSlot(slot);
  AppState::instance().setWifi(WifiStatus::Connecting, "");
  scheduleConnect(kSwitchDelayUs);
}

bool isConnected() { return connected; }

}  // namespace wifi_station

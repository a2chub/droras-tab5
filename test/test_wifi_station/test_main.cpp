#include <unity.h>

#include <array>
#include <cstring>
#include <optional>

// Compile the actual station and shared state against the narrow ESP-IDF substitutes.
// Only test setup touches private globals, to model a power cycle without changing NVS.
#include "../../src/wifi_station.cpp"
#include "../../src/app_state.cpp"

namespace fake {
int64_t now = 1'000'000;
esp_event_handler_t wifiHandler = nullptr;
esp_event_handler_t ipHandler = nullptr;
esp_netif_t netif;
wifi_config_t applied{};
std::array<std::optional<heatboard::WifiProfile>, 4> saved;
std::optional<int> selected;
esp_err_t profileSaveResult = ESP_OK;
esp_err_t slotSaveResult = ESP_OK;
esp_err_t initResult = ESP_OK;
esp_err_t configResult = ESP_OK;
esp_err_t connectResult = ESP_OK;
esp_err_t signalResult = ESP_OK;
int8_t signalRssi = -55;
int signalCalls = 0;
bool disconnectDuringSignal = false;
int initCalls = 0;
int hostedCalls = 0;
int startCalls = 0;
int stopCalls = 0;
int connectCalls = 0;
bool started = false;

void event(int id) {
  TEST_ASSERT_NOT_NULL(wifiHandler);
  wifi_event_sta_disconnected_t data{201};
  wifiHandler(nullptr, WIFI_EVENT, id, &data);
}

void gotIp() {
  TEST_ASSERT_NOT_NULL(ipHandler);
  ip_event_got_ip_t data{{{{192, 0, 2, 10}}}};
  ipHandler(nullptr, IP_EVENT, IP_EVENT_STA_GOT_IP, &data);
}

void advance(int64_t microseconds) {
  now += microseconds;
  wifi_station::poll();
}

void resetRuntime() {
  wifi_station::profiles = {};
  wifi_station::storedProfiles = {};
  wifi_station::connected = false;
  wifi_station::switching = false;
  wifi_station::stopped = false;
  wifi_station::reconnectAtUs = 0;
  wifi_station::connectingSinceUs = 0;
  wifi_station::netifReady = false;
  wifi_station::hostedReady = false;
  wifi_station::driverReady = false;
  wifi_station::wifiHandlerReady = false;
  wifi_station::ipHandlerReady = false;
  wifi_station::radioStarted = false;
  wifi_station::failureReported = false;
  wifi_station::waitingForStop = false;
  wifi_station::stopRequestedUs = 0;
  wifi_station::nextSignalPollUs = 0;
  wifi_station::pendingNetwork = {};
  now = 1'000'000;
  wifiHandler = nullptr;
  ipHandler = nullptr;
  applied = {};
  initCalls = hostedCalls = startCalls = stopCalls = connectCalls = 0;
  signalCalls = 0;
  started = false;
  AppState::instance().setWifi(WifiStatus::Connecting, "");
  AppState::instance().setWifiSlot(-1);
}

void bootConfigured() {
  saved[0] = heatboard::WifiProfile{"test-network", "dummy-password"};
  selected = 0;
  wifi_station::start();
  wifi_station::poll();
}

void assertStatus(WifiStatus expected) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(AppState::instance().snapshot().wifi));
}
}  // namespace fake

int64_t esp_timer_get_time() { return fake::now; }
esp_err_t esp_netif_init() { return ESP_OK; }
esp_err_t esp_event_loop_create_default() { return ESP_OK; }
esp_netif_t* esp_netif_create_default_wifi_sta() { return &fake::netif; }
esp_err_t esp_hosted_init() { ++fake::hostedCalls; return ESP_OK; }
esp_err_t esp_wifi_init(const wifi_init_config_t*) { ++fake::initCalls; return fake::initResult; }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t,
                                             esp_event_handler_t handler, void*, void*) {
  (base == WIFI_EVENT ? fake::wifiHandler : fake::ipHandler) = handler;
  return ESP_OK;
}
esp_err_t esp_wifi_set_storage(int storage) { return storage == WIFI_STORAGE_RAM ? ESP_OK : ESP_FAIL; }
esp_err_t esp_wifi_set_mode(int) { return ESP_OK; }
esp_err_t esp_wifi_set_config(int, const wifi_config_t* config) {
  if (fake::started) return ESP_ERR_INVALID_STATE;
  if (fake::configResult != ESP_OK) return fake::configResult;
  fake::applied = *config;
  return ESP_OK;
}
esp_err_t esp_wifi_start() { ++fake::startCalls; fake::started = true; return ESP_OK; }
esp_err_t esp_wifi_stop() { ++fake::stopCalls; fake::started = false; return ESP_OK; }
esp_err_t esp_wifi_connect() {
  ++fake::connectCalls;
  return fake::started ? fake::connectResult : ESP_ERR_INVALID_STATE;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* accessPoint) {
  ++fake::signalCalls;
  accessPoint->rssi = fake::signalRssi;
  if (fake::disconnectDuringSignal) fake::event(WIFI_EVENT_STA_DISCONNECTED);
  return fake::signalResult;
}

namespace settings_store {
std::optional<heatboard::WifiProfile> loadWifiProfile(int slot) { return fake::saved.at(slot); }
std::optional<int> loadWifiSlot() { return fake::selected; }
esp_err_t saveWifiProfile(int slot, const heatboard::WifiProfile& profile) {
  if (fake::profileSaveResult == ESP_OK) fake::saved.at(slot) = profile;
  return fake::profileSaveResult;
}
esp_err_t saveWifiSlot(int slot) {
  if (fake::slotSaveResult == ESP_OK) fake::selected = slot;
  return fake::slotSaveResult;
}
}  // namespace settings_store

void setUp() {
  fake::saved = {};
  fake::selected.reset();
  fake::profileSaveResult = fake::slotSaveResult = fake::initResult = fake::connectResult = ESP_OK;
  fake::configResult = ESP_OK;
  fake::signalResult = ESP_OK;
  fake::signalRssi = -55;
  fake::disconnectDuringSignal = false;
  fake::resetRuntime();
}
void tearDown() {}

void empty_device_can_save_and_connect_without_reboot() {
  wifi_station::start();
  fake::assertStatus(WifiStatus::NotConfigured);
  TEST_ASSERT_EQUAL_INT(0, fake::initCalls);
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(2, {"test-network", "dummy-password"}));
  wifi_station::switchTo(2);
  wifi_station::poll();
  TEST_ASSERT_EQUAL_INT(1, fake::connectCalls);
  TEST_ASSERT_EQUAL_STRING("test-network", reinterpret_cast<char*>(fake::applied.sta.ssid));
  fake::gotIp();
  TEST_ASSERT_TRUE(wifi_station::isConnected());
  fake::assertStatus(WifiStatus::Connected);
  TEST_ASSERT_EQUAL_STRING("192.0.2.10", AppState::instance().snapshot().ipAddress.c_str());
}

void same_slot_password_change_waits_for_stop_and_ignores_old_events() {
  fake::bootConfigured();
  fake::gotIp();
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(0, {"test-network", "changed-password"}));
  wifi_station::switchTo(0);
  TEST_ASSERT_EQUAL_INT(1, fake::stopCalls);
  TEST_ASSERT_EQUAL_INT(1, fake::startCalls);
  fake::event(WIFI_EVENT_STA_DISCONNECTED);
  fake::gotIp();
  TEST_ASSERT_FALSE(wifi_station::isConnected());
  fake::assertStatus(WifiStatus::Connecting);
  wifi_station::poll();
  TEST_ASSERT_EQUAL_INT(1, fake::startCalls);
  fake::event(WIFI_EVENT_STA_STOP);
  wifi_station::poll();
  wifi_station::poll();
  TEST_ASSERT_EQUAL_INT(2, fake::startCalls);
  TEST_ASSERT_EQUAL_INT(2, fake::connectCalls);
  TEST_ASSERT_EQUAL_STRING("changed-password", reinterpret_cast<char*>(fake::applied.sta.password));
}

void latest_selection_wins_while_waiting_for_stop() {
  fake::bootConfigured();
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(1, {"network-two", ""}));
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(2, {"network-three", ""}));
  wifi_station::switchTo(1);
  wifi_station::switchTo(2);
  TEST_ASSERT_EQUAL_INT(1, fake::stopCalls);
  fake::event(WIFI_EVENT_STA_STOP);
  wifi_station::poll();
  TEST_ASSERT_EQUAL_STRING("network-three", reinterpret_cast<char*>(fake::applied.sta.ssid));
  TEST_ASSERT_EQUAL_INT(2, AppState::instance().snapshot().wifiSlot);
}

void reboot_loads_saved_credentials_and_selection() {
  wifi_station::start();
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(3, {"saved-network", "saved-password"}));
  wifi_station::switchTo(3);
  fake::resetRuntime();  // Emulate a restart while retaining the fake nonvolatile store.
  wifi_station::start();
  TEST_ASSERT_EQUAL_INT(3, AppState::instance().snapshot().wifiSlot);
  TEST_ASSERT_EQUAL_STRING("saved-network", reinterpret_cast<char*>(fake::applied.sta.ssid));
  TEST_ASSERT_EQUAL_STRING("saved-password", reinterpret_cast<char*>(fake::applied.sta.password));
}

void failed_profile_save_preserves_connection_and_profile() {
  fake::bootConfigured();
  fake::gotIp();
  fake::profileSaveResult = ESP_FAIL;
  TEST_ASSERT_EQUAL_INT(ESP_FAIL, wifi_station::saveProfile(0, {"replacement", "new-password"}));
  TEST_ASSERT_EQUAL_STRING("test-network", wifi_station::profile(0).ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("dummy-password", fake::saved[0]->password.c_str());
  TEST_ASSERT_TRUE(wifi_station::isConnected());
  fake::assertStatus(WifiStatus::Connected);
  TEST_ASSERT_EQUAL_INT(0, fake::stopCalls);
}

void failed_slot_save_does_not_disconnect_or_change_selection() {
  fake::bootConfigured();
  fake::gotIp();
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(1, {"another-network", ""}));
  fake::slotSaveResult = ESP_FAIL;
  wifi_station::switchTo(1);
  TEST_ASSERT_TRUE(wifi_station::isConnected());
  TEST_ASSERT_EQUAL_INT(0, fake::stopCalls);
  TEST_ASSERT_EQUAL_INT(0, AppState::instance().snapshot().wifiSlot);
  TEST_ASSERT_EQUAL_INT(0, *fake::selected);
}

void initialization_failure_can_retry_without_double_hosted_init() {
  fake::initResult = ESP_FAIL;
  fake::bootConfigured();
  fake::assertStatus(WifiStatus::Failed);
  TEST_ASSERT_EQUAL_INT(0, fake::startCalls);
  fake::initResult = ESP_OK;
  wifi_station::switchTo(0);
  wifi_station::poll();
  TEST_ASSERT_EQUAL_INT(2, fake::initCalls);
  TEST_ASSERT_EQUAL_INT(1, fake::hostedCalls);
  TEST_ASSERT_EQUAL_INT(1, fake::connectCalls);
  fake::gotIp();
  fake::assertStatus(WifiStatus::Connected);
}

void rejected_configuration_ignores_old_events_and_can_retry() {
  fake::configResult = ESP_FAIL;
  fake::bootConfigured();
  fake::assertStatus(WifiStatus::Failed);
  fake::gotIp();
  fake::event(WIFI_EVENT_STA_DISCONNECTED);
  fake::advance(2'000'000);
  fake::assertStatus(WifiStatus::Failed);
  TEST_ASSERT_FALSE(wifi_station::isConnected());
  TEST_ASSERT_EQUAL_INT(0, fake::connectCalls);
  fake::configResult = ESP_OK;
  wifi_station::switchTo(0);
  wifi_station::poll();
  fake::gotIp();
  fake::assertStatus(WifiStatus::Connected);
}

void driver_connect_errors_retry_and_timeout_then_recover() {
  fake::connectResult = ESP_FAIL;
  fake::bootConfigured();
  fake::advance(1'999'999);
  TEST_ASSERT_EQUAL_INT(1, fake::connectCalls);
  fake::advance(1);
  TEST_ASSERT_EQUAL_INT(2, fake::connectCalls);
  fake::advance(28'000'000);
  fake::assertStatus(WifiStatus::Failed);
  fake::connectResult = ESP_OK;
  fake::advance(2'000'000);
  fake::gotIp();
  fake::assertStatus(WifiStatus::Connected);
}

void disconnection_starts_new_timeout_and_retries_after_two_seconds() {
  fake::bootConfigured();
  fake::gotIp();
  fake::advance(60'000'000);
  fake::event(WIFI_EVENT_STA_DISCONNECTED);
  fake::assertStatus(WifiStatus::Connecting);
  fake::advance(1'999'999);
  TEST_ASSERT_EQUAL_INT(1, fake::connectCalls);
  fake::advance(1);
  TEST_ASSERT_EQUAL_INT(2, fake::connectCalls);
  fake::advance(28'000'000);
  fake::assertStatus(WifiStatus::Failed);
}

void brief_connection_resets_previous_timeout_even_without_poll() {
  fake::bootConfigured();
  fake::advance(30'000'000);
  fake::assertStatus(WifiStatus::Failed);
  fake::gotIp();
  fake::event(WIFI_EVENT_STA_DISCONNECTED);
  fake::assertStatus(WifiStatus::Connecting);
  fake::advance(30'000'000);
  fake::assertStatus(WifiStatus::Failed);
}

void signal_is_sampled_every_two_seconds_and_cleared_on_disconnect() {
  fake::bootConfigured();
  TEST_ASSERT_FALSE(AppState::instance().snapshot().wifiRssiDbm.has_value());
  TEST_ASSERT_EQUAL_INT(0, fake::signalCalls);
  fake::gotIp();
  wifi_station::poll();
  TEST_ASSERT_EQUAL_INT(-55, AppState::instance().snapshot().wifiRssiDbm.value_or(0));
  TEST_ASSERT_EQUAL_INT(1, fake::signalCalls);
  fake::signalRssi = -78;
  fake::advance(1'999'999);
  TEST_ASSERT_EQUAL_INT(1, fake::signalCalls);
  fake::advance(1);
  TEST_ASSERT_EQUAL_INT(2, fake::signalCalls);
  TEST_ASSERT_EQUAL_INT(-78, AppState::instance().snapshot().wifiRssiDbm.value_or(0));
  fake::event(WIFI_EVENT_STA_DISCONNECTED);
  TEST_ASSERT_FALSE(AppState::instance().snapshot().wifiRssiDbm.has_value());
}

void failed_signal_queries_do_not_keep_old_strength() {
  fake::bootConfigured();
  fake::gotIp();
  wifi_station::poll();
  fake::signalResult = ESP_FAIL;
  fake::advance(2'000'000);
  TEST_ASSERT_FALSE(AppState::instance().snapshot().wifiRssiDbm.has_value());
  fake::assertStatus(WifiStatus::Connected);
  fake::signalResult = ESP_OK;
  fake::signalRssi = 0;
  fake::advance(2'000'000);
  TEST_ASSERT_FALSE(AppState::instance().snapshot().wifiRssiDbm.has_value());
  fake::signalRssi = -60;
  fake::advance(2'000'000);
  TEST_ASSERT_EQUAL_INT(-60, AppState::instance().snapshot().wifiRssiDbm.value_or(0));
  const auto revision = AppState::instance().revision();
  fake::advance(2'000'000);
  TEST_ASSERT_EQUAL_UINT32(revision, AppState::instance().revision());
}

void signal_query_finishing_after_disconnect_does_not_publish_stale_sample() {
  fake::bootConfigured();
  fake::gotIp();
  fake::disconnectDuringSignal = true;
  wifi_station::poll();
  TEST_ASSERT_FALSE(AppState::instance().snapshot().wifiRssiDbm.has_value());
  fake::assertStatus(WifiStatus::Connecting);
}

void maximum_length_ssid_and_hex_psk_are_not_truncated() {
  wifi_station::start();
  const heatboard::WifiProfile profile{std::string(32, 's'), std::string(64, 'a')};
  TEST_ASSERT_EQUAL_INT(ESP_OK, wifi_station::saveProfile(0, profile));
  wifi_station::switchTo(0);
  TEST_ASSERT_EQUAL_MEMORY(profile.ssid.data(), fake::applied.sta.ssid, 32);
  TEST_ASSERT_EQUAL_MEMORY(profile.password.data(), fake::applied.sta.password, 64);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(empty_device_can_save_and_connect_without_reboot);
  RUN_TEST(same_slot_password_change_waits_for_stop_and_ignores_old_events);
  RUN_TEST(latest_selection_wins_while_waiting_for_stop);
  RUN_TEST(reboot_loads_saved_credentials_and_selection);
  RUN_TEST(failed_profile_save_preserves_connection_and_profile);
  RUN_TEST(failed_slot_save_does_not_disconnect_or_change_selection);
  RUN_TEST(initialization_failure_can_retry_without_double_hosted_init);
  RUN_TEST(rejected_configuration_ignores_old_events_and_can_retry);
  RUN_TEST(driver_connect_errors_retry_and_timeout_then_recover);
  RUN_TEST(disconnection_starts_new_timeout_and_retries_after_two_seconds);
  RUN_TEST(brief_connection_resets_previous_timeout_even_without_poll);
  RUN_TEST(maximum_length_ssid_and_hex_psk_are_not_truncated);
  RUN_TEST(signal_is_sampled_every_two_seconds_and_cleared_on_disconnect);
  RUN_TEST(failed_signal_queries_do_not_keep_old_strength);
  RUN_TEST(signal_query_finishing_after_disconnect_does_not_publish_stale_sample);
  return UNITY_END();
}

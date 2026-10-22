// Settings that survive a reboot (NVS).
#pragma once

#include <optional>

#include "esp_err.h"
#include "heatboard/gesture_control.h"
#include "heatboard/orientation.h"
#include "heatboard/server_address.h"
#include "heatboard/wifi_profile.h"

namespace settings_store {

// Must run once before any other call.
esp_err_t init();

// nullopt when no Raspberry Pi server has been configured (or the stored value is unusable).
std::optional<heatboard::ServerAddress> loadServerAddress();

// Passing nullopt removes the setting, which disables the Raspberry Pi connection.
esp_err_t saveServerAddress(const std::optional<heatboard::ServerAddress>& address);

// Index (0-based) of the Wi-Fi profile chosen on the settings screen; nullopt if never chosen.
std::optional<int> loadWifiSlot();
esp_err_t saveWifiSlot(int slot);

// Each slot stores SSID and password as a single versioned value. Missing,
// unsupported or invalid records return nullopt; invalid input is never saved.
std::optional<heatboard::WifiProfile> loadWifiProfile(int slot);
esp_err_t saveWifiProfile(int slot, const heatboard::WifiProfile& profile);

// Speaker volume and screen brightness levels as set on the settings screen (see
// ui::kLevelMax); nullopt if never changed.
std::optional<int> loadVolumeLevel();
esp_err_t saveVolumeLevel(int level);
std::optional<int> loadBrightnessLevel();
esp_err_t saveBrightnessLevel(int level);

// Screen orientation mode from the settings screen; nullopt if never chosen (or unusable).
std::optional<heatboard::OrientationMode> loadOrientationMode();
esp_err_t saveOrientationMode(heatboard::OrientationMode mode);

// Hand gesture operation: on/off, hold time and which gesture does what. Fields never
// saved (or unusable) come back at their defaults.
heatboard::GestureSettings loadGestureSettings();
esp_err_t saveGestureSettings(const heatboard::GestureSettings& settings);

}  // namespace settings_store

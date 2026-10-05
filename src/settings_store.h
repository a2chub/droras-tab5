// Settings that survive a reboot (NVS).
#pragma once

#include <optional>

#include "esp_err.h"
#include "heatboard/server_address.h"

namespace settings_store {

// Must run once before any other call.
esp_err_t init();

// nullopt when no Raspberry Pi server has been configured (or the stored value is unusable).
std::optional<heatboard::ServerAddress> loadServerAddress();

// Passing nullopt removes the setting, which disables the Raspberry Pi connection.
esp_err_t saveServerAddress(const std::optional<heatboard::ServerAddress>& address);

// Index (0-based) of the Wi-Fi preset chosen on the settings screen; nullopt if never chosen.
std::optional<int> loadWifiSlot();
esp_err_t saveWifiSlot(int slot);

// Speaker volume and screen brightness levels as set on the settings screen (see
// ui::kLevelMax); nullopt if never changed.
std::optional<int> loadVolumeLevel();
esp_err_t saveVolumeLevel(int level);
std::optional<int> loadBrightnessLevel();
esp_err_t saveBrightnessLevel(int level);

}  // namespace settings_store

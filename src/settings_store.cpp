#include "settings_store.h"

#include <algorithm>
#include <array>
#include <string>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace settings_store {

namespace {

const char* TAG = "settings";
const char* kNamespace = "heatboard";
const char* kServerAddressKey = "rpi_addr";
const char* kWifiSlotKey = "wifi_slot";
const char* kWifiProfileKeys[heatboard::kWifiNetworkSlots] = {
    "wifi_profile0", "wifi_profile1", "wifi_profile2", "wifi_profile3"};
const char* kVolumeKey = "volume";
const char* kBrightnessKey = "brightness";
const char* kOrientationModeKey = "orient_mode";
const char* kGestureEnabledKey = "gest_on";
const char* kGestureHoldKey = "gest_hold";  // tenths of a second
const char* kGestureActionKeys[heatboard::kBoardActionCount] = {"gest_prev", "gest_next", "gest_start"};

std::optional<int> loadSmallInt(const char* key) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    // The namespace does not exist until something has been saved.
    return std::nullopt;
  }
  int8_t value = 0;
  const esp_err_t err = nvs_get_i8(handle, key, &value);
  nvs_close(handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "reading %s failed: %s", key, esp_err_to_name(err));
    }
    return std::nullopt;
  }
  return value;
}

esp_err_t saveSmallInt(const char* key, int value) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    err = nvs_set_i8(handle, key, static_cast<int8_t>(value));
    if (err == ESP_OK) {
      err = nvs_commit(handle);
    }
    nvs_close(handle);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
  }
  return err;
}

}  // namespace

esp_err_t init() {
  esp_err_t err = nvs_flash_init();
  // A partition written by an older layout cannot be read; settings are few and
  // re-enterable on screen, so erasing is the recovery.
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition unusable (%s), erasing", esp_err_to_name(err));
    err = nvs_flash_erase();
    if (err == ESP_OK) {
      err = nvs_flash_init();
    }
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
  }
  return err;
}

std::optional<heatboard::ServerAddress> loadServerAddress() {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    // ESP_ERR_NVS_NOT_FOUND just means nothing was ever saved.
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
    return std::nullopt;
  }

  char text[32] = {};
  size_t length = sizeof(text);
  err = nvs_get_str(handle, kServerAddressKey, text, &length);
  nvs_close(handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "reading %s failed: %s", kServerAddressKey, esp_err_to_name(err));
    }
    return std::nullopt;
  }

  auto address = heatboard::parseServerAddress(text);
  if (!address) {
    ESP_LOGW(TAG, "stored server address \"%s\" is invalid, ignoring", text);
  }
  return address;
}

esp_err_t saveServerAddress(const std::optional<heatboard::ServerAddress>& address) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return err;
  }

  if (address) {
    err = nvs_set_str(handle, kServerAddressKey, heatboard::formatServerAddress(*address).c_str());
  } else {
    err = nvs_erase_key(handle, kServerAddressKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      err = ESP_OK;
    }
  }
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "saving server address failed: %s", esp_err_to_name(err));
  }
  return err;
}

std::optional<int> loadWifiSlot() { return loadSmallInt(kWifiSlotKey); }
esp_err_t saveWifiSlot(int slot) { return saveSmallInt(kWifiSlotKey, slot); }

std::optional<heatboard::WifiProfile> loadWifiProfile(int slot) {
  if (slot < 0 || slot >= heatboard::kWifiNetworkSlots) return std::nullopt;
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "opening Wi-Fi profile failed: %s", esp_err_to_name(err));
    }
    return std::nullopt;
  }

  std::array<uint8_t, heatboard::kWifiProfileBlobMaxBytes> data{};
  size_t size = data.size();
  err = nvs_get_blob(handle, kWifiProfileKeys[slot], data.data(), &size);
  nvs_close(handle);
  if (err != ESP_OK) {
    if (err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGW(TAG, "reading Wi-Fi profile %d failed: %s", slot, esp_err_to_name(err));
    }
    return std::nullopt;
  }
  auto profile = heatboard::decodeWifiProfile(data.data(), size);
  if (!profile) ESP_LOGW(TAG, "stored Wi-Fi profile %d is invalid, ignoring", slot);
  return profile;
}

esp_err_t saveWifiProfile(int slot, const heatboard::WifiProfile& profile) {
  if (slot < 0 || slot >= heatboard::kWifiNetworkSlots) return ESP_ERR_INVALID_ARG;
  const auto data = heatboard::encodeWifiProfile(profile);
  if (!data) return ESP_ERR_INVALID_ARG;

  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err == ESP_OK) {
    // NVS writes the pair as one blob: an interrupted replacement cannot combine
    // a new SSID with the previous password (or the reverse).
    err = nvs_set_blob(handle, kWifiProfileKeys[slot], data->data(), data->size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "saving Wi-Fi profile %d failed: %s", slot, esp_err_to_name(err));
  }
  return err;
}

std::optional<int> loadVolumeLevel() { return loadSmallInt(kVolumeKey); }
esp_err_t saveVolumeLevel(int level) { return saveSmallInt(kVolumeKey, level); }

std::optional<int> loadBrightnessLevel() { return loadSmallInt(kBrightnessKey); }
esp_err_t saveBrightnessLevel(int level) { return saveSmallInt(kBrightnessKey, level); }

std::optional<heatboard::OrientationMode> loadOrientationMode() {
  const auto value = loadSmallInt(kOrientationModeKey);
  if (!value) {
    return std::nullopt;
  }
  const auto mode = heatboard::orientationModeFromInt(*value);
  if (!mode) {
    ESP_LOGW(TAG, "stored orientation mode %d is invalid, ignoring", *value);
  }
  return mode;
}

esp_err_t saveOrientationMode(heatboard::OrientationMode mode) {
  return saveSmallInt(kOrientationModeKey, static_cast<int>(mode));
}

heatboard::GestureSettings loadGestureSettings() {
  heatboard::GestureSettings settings;
  if (const auto on = loadSmallInt(kGestureEnabledKey)) {
    settings.enabled = *on != 0;
  }
  if (const auto hold = loadSmallInt(kGestureHoldKey)) {
    settings.holdMs = std::clamp(*hold * 100, heatboard::kMinHoldMs, heatboard::kMaxHoldMs);
  }
  for (int i = 0; i < heatboard::kBoardActionCount; ++i) {
    if (const auto value = loadSmallInt(kGestureActionKeys[i])) {
      if (const auto gesture = heatboard::gestureFromInt(*value)) {
        settings.assignment[i] = *gesture;
      } else {
        ESP_LOGW(TAG, "stored gesture %d for %s is invalid, ignoring", *value, kGestureActionKeys[i]);
      }
    }
  }
  return settings;
}

esp_err_t saveGestureSettings(const heatboard::GestureSettings& settings) {
  esp_err_t err = saveSmallInt(kGestureEnabledKey, settings.enabled ? 1 : 0);
  if (err == ESP_OK) {
    err = saveSmallInt(kGestureHoldKey, settings.holdMs / 100);
  }
  for (int i = 0; i < heatboard::kBoardActionCount && err == ESP_OK; ++i) {
    err = saveSmallInt(kGestureActionKeys[i], static_cast<int>(settings.assignment[i]));
  }
  return err;
}

}  // namespace settings_store

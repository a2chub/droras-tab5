#include "settings_store.h"

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
const char* kVolumeKey = "volume";
const char* kBrightnessKey = "brightness";

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

std::optional<int> loadVolumeLevel() { return loadSmallInt(kVolumeKey); }
esp_err_t saveVolumeLevel(int level) { return saveSmallInt(kVolumeKey, level); }

std::optional<int> loadBrightnessLevel() { return loadSmallInt(kBrightnessKey); }
esp_err_t saveBrightnessLevel(int level) { return saveSmallInt(kBrightnessKey, level); }

}  // namespace settings_store

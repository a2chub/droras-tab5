#pragma once
#include <cstdint>
#include "esp_err.h"
struct wifi_init_config_t {};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
struct wifi_config_t { struct { uint8_t ssid[32]; uint8_t password[64]; } sta; };
struct wifi_event_sta_disconnected_t { uint8_t reason = 0; };
struct wifi_ap_record_t { int8_t rssi = 0; };
constexpr int WIFI_IF_STA = 0;
constexpr int WIFI_MODE_STA = 1;
constexpr int WIFI_STORAGE_RAM = 0;
esp_err_t esp_wifi_init(const wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t*);
esp_err_t esp_wifi_start();
esp_err_t esp_wifi_stop();
esp_err_t esp_wifi_connect();
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);

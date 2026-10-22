#pragma once
#include <cstdint>
#include "esp_err.h"
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
inline constexpr esp_event_base_t WIFI_EVENT = "wifi";
inline constexpr esp_event_base_t IP_EVENT = "ip";
constexpr int ESP_EVENT_ANY_ID = -1;
constexpr int WIFI_EVENT_STA_START = 1;
constexpr int WIFI_EVENT_STA_STOP = 2;
constexpr int WIFI_EVENT_STA_CONNECTED = 3;
constexpr int WIFI_EVENT_STA_DISCONNECTED = 4;
constexpr int IP_EVENT_STA_GOT_IP = 5;
esp_err_t esp_event_loop_create_default();
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t,
                                             esp_event_handler_t, void*, void*);

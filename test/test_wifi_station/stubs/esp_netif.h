#pragma once
#include <cstdint>
#include "esp_err.h"
struct esp_netif_t {};
struct esp_ip4_addr_t { uint8_t bytes[4]; };
struct ip_event_got_ip_t { struct { esp_ip4_addr_t ip; } ip_info; };
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) (ip)->bytes[0], (ip)->bytes[1], (ip)->bytes[2], (ip)->bytes[3]
esp_err_t esp_netif_init();
esp_netif_t* esp_netif_create_default_wifi_sta();

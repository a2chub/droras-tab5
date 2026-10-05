// Wi-Fi networks this device may join.
// Copy this file to app_secrets.h (git-ignored) and fill in up to four networks; which one is
// used is chosen on the settings screen.
//   LABEL    text shown on the settings-screen button (e.g. "会場", "自宅"); the SSID is shown if empty
//   SSID     network name; leave empty ("") for unused slots
//   PASSWORD leave empty for open networks
// The Tab5's radio (ESP32-C6) supports 2.4GHz networks only.
#pragma once

#define APP_WIFI_1_LABEL ""
#define APP_WIFI_1_SSID ""
#define APP_WIFI_1_PASSWORD ""

#define APP_WIFI_2_LABEL ""
#define APP_WIFI_2_SSID ""
#define APP_WIFI_2_PASSWORD ""

#define APP_WIFI_3_LABEL ""
#define APP_WIFI_3_SSID ""
#define APP_WIFI_3_PASSWORD ""

#define APP_WIFI_4_LABEL ""
#define APP_WIFI_4_SSID ""
#define APP_WIFI_4_PASSWORD ""

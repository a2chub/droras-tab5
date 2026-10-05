// Wi-Fi station on the Tab5's ESP32-C6 co-processor.
//
// Up to kNetworkSlots networks are compiled in from app_secrets.h; one of them is active
// at a time and the choice is remembered across reboots.
#pragma once

namespace wifi_station {

inline constexpr int kNetworkSlots = 4;

// SSID of a preset, or "" for an unused slot or an out-of-range index.
const char* ssid(int slot);

// Name to show for a preset: its label from app_secrets.h, or the SSID when no label is set.
const char* label(int slot);

// Brings the station up on the remembered preset (or the first one filled in) and keeps it
// connected, reporting progress through AppState. Call after M5.begin(): that is what
// powers the C6.
void start();

// Switches to another preset, remembers it and reconnects. Ignores unused slots.
// Call from the same task as start().
void switchTo(int slot);

bool isConnected();

}  // namespace wifi_station

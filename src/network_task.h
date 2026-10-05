// Background task that owns all network activity: Wi-Fi, the pilot list download, the
// Raspberry Pi connection and the Firestore fallback.
#pragma once

namespace network_task {

// Call once, after M5.begin() and settings_store::init().
void start();

}  // namespace network_task

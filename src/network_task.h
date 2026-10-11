// Background tasks that own all network activity: one for the LAN side (Wi-Fi and the
// Raspberry Pi connection), one for the internet side (pilot list download and the
// Firestore fallback), so that the former is never held up by the latter timing out.
#pragma once

namespace network_task {

// Call once, after M5.begin() and settings_store::init().
void start();

}  // namespace network_task

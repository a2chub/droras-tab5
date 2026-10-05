// Fixed endpoints and timings of the heat board.
#pragma once

namespace app_config {

// Google Apps Script endpoint serving the pilot list as CSV (JDLid,name,class,heat).
// Same URL the droras server downloads its heat list from.
inline constexpr char kHeatListUrl[] =
    "https://script.google.com/macros/s/"
    "AKfycbwUKwnl136z5HUvhsN7amnuis_GrTmyvZbgRF1GpXU1vKycaHjBXgLyKD8zuK7hIG-c/exec";

// Firestore document the droras server writes the current heat number to ({heat: "12"}).
// Publicly readable; this is what info.japandroneleague.com displays.
inline constexpr char kFirestoreCurrentHeatUrl[] =
    "https://firestore.googleapis.com/v1/projects/jdl-main/databases/(default)/documents/race/current";

// Firestore is only polled while the Raspberry Pi is unreachable. The project's free tier
// allows 50,000 document reads per day shared with the public site; 3s keeps one board
// under 29,000 even if it falls back for a whole day.
inline constexpr int kFirestorePollIntervalMs = 3000;
inline constexpr int kFirestoreFailuresBeforeOffline = 3;

inline constexpr int kHeatListRetryMinMs = 2000;
inline constexpr int kHeatListRetryMaxMs = 30000;

// Length of the progress bar started by the start button; when it runs out the board moves
// on to the next heat. Same value as the droras operator UI (front/src/pages/ProgressBar.tsx).
inline constexpr int kRaceTimerSeconds = 150;

}  // namespace app_config

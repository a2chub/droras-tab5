#include "screen_orientation.h"

#include <M5Unified.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "settings_store.h"
#include "ui_common.h"

namespace screen_orientation {

namespace {

using heatboard::Orientation;
using heatboard::OrientationMode;

const char* TAG = "orientation";

// Fast enough that the hold time, not the polling, decides how quickly the screen turns.
constexpr int64_t kPollIntervalMs = 50;

OrientationMode currentMode = OrientationMode::Auto;
heatboard::OrientationDetector detector;
bool available = false;
int64_t lastPollMs = 0;
float lastAccel[3] = {};
float lastDownG = 0.0f;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Gravity along the downward axis of the UI as it is shown normally, in g. In landscape the
// UI's vertical runs along the panel's short side, which is the accelerometer's X axis as
// M5Unified reports it. Measured on the device: X is about -1g held the normal way up,
// +1g upside down, and Z is about -1g lying flat.
float downAxisG(const float accel[3]) { return -accel[0]; }

// Reads the sensor into the detector. False when there was nothing to read.
bool poll() {
  if (!available) {
    return false;
  }
  if (!M5.Imu.getAccel(&lastAccel[0], &lastAccel[1], &lastAccel[2])) {
    return false;
  }
  lastDownG = downAxisG(lastAccel);
  detector.update(lastDownG, nowMs());
  return true;
}

void apply() { ui::setUpsideDown(resolveOrientation(currentMode, detector.orientation()) == Orientation::UpsideDown); }

}  // namespace

void init() {
  currentMode = settings_store::loadOrientationMode().value_or(OrientationMode::Auto);
  available = M5.Imu.isEnabled();
  if (!available) {
    ESP_LOGW(TAG, "no accelerometer found; automatic orientation keeps the screen upright");
  }
  poll();
  lastPollMs = nowMs();
  apply();
}

void update() {
  const int64_t now = nowMs();
  if (now - lastPollMs < kPollIntervalMs) {
    return;
  }
  lastPollMs = now;
  if (poll()) {
    apply();
  }
}

OrientationMode mode() { return currentMode; }

void setMode(OrientationMode mode) {
  currentMode = mode;
  apply();
}

bool sensorAvailable() { return available; }

Orientation sensed() { return detector.orientation(); }

void logReading() {
  ESP_LOGI(TAG, "accel x=%.2f y=%.2f z=%.2f down=%.2f sensed=%s mode=%d screen=%s", lastAccel[0], lastAccel[1],
           lastAccel[2], lastDownG, sensed() == Orientation::UpsideDown ? "upside-down" : "normal",
           static_cast<int>(currentMode), ui::upsideDown() ? "upside-down" : "upright");
}

}  // namespace screen_orientation

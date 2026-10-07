// Operating the board by hand gestures in front of the Tab5's camera: a hand held up is
// detected and classified (one, two, five, like, ...) by ESP-DL models on the second core,
// and a gesture held for the configured time becomes a button press (see
// heatboard/gesture_control.h for the rules and settings).
#pragma once

#include <optional>

#include "heatboard/gesture_control.h"

namespace hand_gesture {

// Opens the I2C bus the camera sensor shares with the touch panel and IMU. Must run before
// M5.begin(): M5GFX then finds the bus already open and shares it instead of taking the
// port over. Returns false (after logging) if the bus could not be created; the camera is
// then unavailable, everything else still works.
bool prepareSharedI2c();

// Brings the camera up with `settings` and starts the recognition task. Must run on the
// main task after M5.begin(): sensor configuration goes over the shared I2C bus, which
// M5.update() uses from the main task too. Without a camera the task is not started and
// gestures stay off whatever the settings say.
void start(const heatboard::GestureSettings& settings);
bool cameraAvailable();

// The settings in use. setSettings() applies at once; storing them is the caller's job.
heatboard::GestureSettings settings();
void setSettings(const heatboard::GestureSettings& settings);

// What the recognition is doing right now, for the screens.
struct Status {
  heatboard::Gesture seen = heatboard::Gesture::None;  // in the latest frame (None: no usable hand)
  float seenScore = 0.0f;
  heatboard::Gesture holding = heatboard::Gesture::None;  // counting towards a trigger
  float progress = 0.0f;                                   // of the hold, 0..1
};
Status status();

// The next action a held gesture triggered, if any. Call every loop iteration.
std::optional<heatboard::BoardAction> takeAction();

// Exposure control. The ISP's own controller is not used (it would talk to the sensor from
// a task of its own over the shared I2C bus), so the main task adjusts exposure and gain
// instead: call every loop iteration, it acts every few hundred ms from the brightness the
// recognition task measured on the latest frame.
void updateExposure();
// Fixes exposure (sensor lines) and gain (index) by hand and stops the automatic control,
// or with exposure < 0 hands control back to it (debug console "cam").
void setExposure(int exposure, int gain);

// Runs the models once on Espressif's sample picture built into the firmware and logs the
// result (debug console "camtest"): one hand showing "one" is expected. Tells a camera
// problem from a model one.
void requestSelfTest();

// Makes the recognition behave as if the camera saw `gesture` (a heatboard::Gesture value)
// for the next `durationMs` (debug console "gest"), to try the hold, the indicator and the
// resulting button press without a camera or good light.
void injectGesture(int gesture, int durationMs);

// Writes the image the models analyse to the serial port in the screenshot format (debug
// console "camshot"). Carried out by the recognition task.
void requestFrameDump();

}  // namespace hand_gesture

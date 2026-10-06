// Keeps the landscape UI the right way up: applies the orientation mode chosen on the
// settings screen and, in Auto, follows the accelerometer when the Tab5 is turned upside down.
#pragma once

#include "heatboard/orientation.h"

namespace screen_orientation {

// Loads the stored mode, reads the sensor once and turns the screen accordingly. Call once,
// after ui::init() and settings_store::init(), before the first screen is drawn.
void init();

// Call every loop iteration. Reads the sensor every so often and turns the screen when the
// orientation to show has changed.
void update();

heatboard::OrientationMode mode();
// Applies `mode` at once. Saving it is up to the caller.
void setMode(heatboard::OrientationMode mode);

// False when the Tab5's accelerometer did not respond; Auto then stays the normal way up.
bool sensorAvailable();
// What the accelerometer says, whatever the mode.
heatboard::Orientation sensed();

// Logs the latest accelerometer reading and the decision made from it (debug console).
void logReading();

}  // namespace screen_orientation

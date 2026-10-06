// Which way up the landscape screen is shown: chosen by hand, or followed from the
// accelerometer so that the board reads correctly when the Tab5 is turned upside down.
#pragma once

#include <cstdint>
#include <optional>

namespace heatboard {

enum class Orientation { Normal, UpsideDown };

// Stored in NVS as its numeric value: keep the order.
enum class OrientationMode { Auto = 0, Normal = 1, UpsideDown = 2 };

// nullopt for a value that is not a mode (e.g. a corrupted setting).
std::optional<OrientationMode> orientationModeFromInt(int value);

// The orientation to show: the fixed one in a manual mode, `sensed` in Auto.
Orientation resolveOrientation(OrientationMode mode, Orientation sensed);

// Turns accelerometer readings into an orientation, ignoring what a hand or a tap does to
// the sensor for a moment.
//
// Each reading is the gravity along the screen's downward axis in g: about +1 when the
// screen is held the normal way up, -1 upside down, 0 when it lies flat. The reading must
// pass a threshold on the other side, and stay there for a while, before the orientation
// flips; lying flat (or anything in between) keeps the last orientation.
class OrientationDetector {
 public:
  // About 30 degrees of tilt away from lying flat.
  static constexpr float kThresholdG = 0.5f;
  static constexpr int64_t kHoldMs = 600;

  // Returns the orientation after taking `downG` (measured at `nowMs`) into account.
  Orientation update(float downG, int64_t nowMs);

  // Normal until the first clear reading. That first one is adopted at once, so the board
  // starts the right way up instead of flipping shortly after power-on.
  Orientation orientation() const { return current_.value_or(Orientation::Normal); }

 private:
  std::optional<Orientation> current_;
  std::optional<Orientation> candidate_;
  int64_t candidateSinceMs_ = 0;
};

}  // namespace heatboard

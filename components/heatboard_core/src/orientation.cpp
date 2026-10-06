#include "heatboard/orientation.h"

namespace heatboard {

std::optional<OrientationMode> orientationModeFromInt(int value) {
  switch (value) {
    case static_cast<int>(OrientationMode::Auto): return OrientationMode::Auto;
    case static_cast<int>(OrientationMode::Normal): return OrientationMode::Normal;
    case static_cast<int>(OrientationMode::UpsideDown): return OrientationMode::UpsideDown;
    default: return std::nullopt;
  }
}

Orientation resolveOrientation(OrientationMode mode, Orientation sensed) {
  switch (mode) {
    case OrientationMode::Normal: return Orientation::Normal;
    case OrientationMode::UpsideDown: return Orientation::UpsideDown;
    case OrientationMode::Auto: break;
  }
  return sensed;
}

Orientation OrientationDetector::update(float downG, int64_t nowMs) {
  std::optional<Orientation> reading;
  if (downG >= kThresholdG) {
    reading = Orientation::Normal;
  } else if (downG <= -kThresholdG) {
    reading = Orientation::UpsideDown;
  }

  if (!current_) {
    current_ = reading;
  } else if (!reading || *reading == *current_) {
    // Back to the current side (or undecided) before the hold ran out: it was a jolt.
    candidate_.reset();
  } else if (candidate_ != reading) {
    candidate_ = reading;
    candidateSinceMs_ = nowMs;
  } else if (nowMs - candidateSinceMs_ >= kHoldMs) {
    current_ = reading;
    candidate_.reset();
  }
  return orientation();
}

}  // namespace heatboard

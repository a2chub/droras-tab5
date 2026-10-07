#include "heatboard/gesture_control.h"

#include <algorithm>
#include <cstring>

namespace heatboard {

Gesture gestureFromLabel(const char* label) {
  if (label == nullptr) {
    return Gesture::None;
  }
  struct Entry {
    const char* label;
    Gesture gesture;
  };
  // Category names of Espressif's hand gesture classifier (HaGRID subset).
  static constexpr Entry kEntries[] = {
      {"one", Gesture::One},   {"two", Gesture::Two}, {"three", Gesture::Three},
      {"four", Gesture::Four}, {"five", Gesture::Five}, {"like", Gesture::Like},
      {"ok", Gesture::Ok},     {"call", Gesture::Call}, {"dislike", Gesture::Dislike},
  };
  for (const Entry& entry : kEntries) {
    if (std::strcmp(label, entry.label) == 0) {
      return entry.gesture;
    }
  }
  return Gesture::None;
}

std::optional<Gesture> gestureFromInt(int value) {
  if (value < 0 || value >= kGestureCount) {
    return std::nullopt;
  }
  return static_cast<Gesture>(value);
}

std::optional<BoardAction> GestureSettings::actionOf(Gesture gesture) const {
  if (gesture == Gesture::None) {
    return std::nullopt;
  }
  for (int i = 0; i < kBoardActionCount; ++i) {
    if (assignment[i] == gesture) {
      return static_cast<BoardAction>(i);
    }
  }
  return std::nullopt;
}

std::optional<BoardAction> GestureTrigger::update(Gesture seen, int64_t nowMs, const GestureSettings& settings) {
  if (!settings.enabled) {
    reset();
    return std::nullopt;
  }

  if (seen == Gesture::None) {
    if (candidate_ != Gesture::None && nowMs - lastSeenMs_ > kGapToleranceMs) {
      // The hand is gone: whatever was held is over, and a fired hold is released.
      candidate_ = Gesture::None;
      armed_ = false;
    }
    return std::nullopt;
  }

  lastSeenMs_ = nowMs;
  if (seen != candidate_) {
    candidate_ = seen;
    candidateSinceMs_ = nowMs;
    // A new gesture right after a trigger (without the hand leaving) is treated as a fresh
    // hold: the operator changed their mind, not the camera flickering.
    armed_ = true;
  }
  if (!armed_) {
    return std::nullopt;
  }
  if (nowMs - candidateSinceMs_ < settings.holdMs) {
    return std::nullopt;
  }
  // Held long enough: fire once, unless an unassigned gesture or the cooldown.
  armed_ = false;
  if (firedAtMs_ >= 0 && nowMs - firedAtMs_ < kCooldownMs) {
    return std::nullopt;
  }
  const auto action = settings.actionOf(candidate_);
  if (action) {
    firedAtMs_ = nowMs;
  }
  return action;
}

float GestureTrigger::progress(int64_t nowMs, const GestureSettings& settings) const {
  if (!armed_ || candidate_ == Gesture::None || !settings.actionOf(candidate_)) {
    return 0.0f;
  }
  const int hold = std::max(settings.holdMs, 1);
  return std::clamp(static_cast<float>(nowMs - candidateSinceMs_) / hold, 0.0f, 1.0f);
}

void GestureTrigger::reset() {
  candidate_ = Gesture::None;
  armed_ = false;
}

}  // namespace heatboard

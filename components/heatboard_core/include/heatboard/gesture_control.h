// Turning what the camera sees (a gesture, frame by frame) into operator commands, with
// the settings that decide which gesture does what and how long it has to be held.
#pragma once

#include <cstdint>
#include <optional>

namespace heatboard {

// The gestures the classifier can report, in the order they are offered on the settings
// screen. Stored in NVS by numeric value: keep the order, append only.
enum class Gesture { None = 0, One, Two, Three, Four, Five, Like, Ok, Call, Dislike };
constexpr int kGestureCount = 10;

// The classifier's category name ("one", "like", ...) to a gesture; None for the names
// that mean "nothing to act on" (no_hand, no_gesture) and anything unknown.
Gesture gestureFromLabel(const char* label);
// nullopt for a value that is not a gesture (e.g. a corrupted setting).
std::optional<Gesture> gestureFromInt(int value);

// What the board can be told to do. Index into GestureSettings::assignment.
enum class BoardAction { PreviousHeat = 0, NextHeat, StartStop };
constexpr int kBoardActionCount = 3;

struct GestureSettings {
  bool enabled = false;
  // How long the same gesture has to be seen before its action fires.
  int holdMs = 800;
  // The gesture assigned to each BoardAction; None leaves the action without a gesture.
  Gesture assignment[kBoardActionCount] = {Gesture::One, Gesture::Two, Gesture::Like};

  // The action `gesture` is assigned to, if any (the first one when assigned twice).
  std::optional<BoardAction> actionOf(Gesture gesture) const;
};

constexpr int kMinHoldMs = 300;
constexpr int kMaxHoldMs = 2000;

// Decides when a gesture becomes a command. The same gesture has to be reported for
// holdMs; short gaps (a frame where the hand was missed) do not restart the count, a
// different gesture does. After firing, the hand has to go away before the same or
// another gesture can fire again, so that a hand kept up does not repeat the command.
class GestureTrigger {
 public:
  // Gaps between sightings of the gesture that do not interrupt a hold.
  static constexpr int64_t kGapToleranceMs = 400;
  // After an action fired, nothing fires for this long.
  static constexpr int64_t kCooldownMs = 1000;

  // Feeds the gesture seen in the latest frame (None when no usable hand was seen). Returns
  // the action to perform, when one has just been triggered.
  std::optional<BoardAction> update(Gesture seen, int64_t nowMs, const GestureSettings& settings);

  // The gesture currently being held towards a trigger, and how far along the hold is
  // (0..1). None / 0 when nothing is being held or the hold already fired.
  Gesture holding() const { return armed_ ? candidate_ : Gesture::None; }
  float progress(int64_t nowMs, const GestureSettings& settings) const;

  // Forgets any hold in progress (e.g. when gestures are switched off).
  void reset();

 private:
  Gesture candidate_ = Gesture::None;
  int64_t candidateSinceMs_ = 0;
  int64_t lastSeenMs_ = 0;
  bool armed_ = false;  // a hold is counting towards a trigger
  int64_t firedAtMs_ = -1;
};

}  // namespace heatboard

#include <unity.h>

#include "heatboard/gesture_control.h"
#include "test_suites.h"

using heatboard::BoardAction;
using heatboard::Gesture;
using heatboard::gestureFromInt;
using heatboard::gestureFromLabel;
using heatboard::GestureSettings;
using heatboard::GestureTrigger;

namespace {

GestureSettings enabledSettings() {
  GestureSettings settings;
  settings.enabled = true;
  settings.holdMs = 800;
  return settings;
}

void assertAction(BoardAction expected, const std::optional<BoardAction>& actual) {
  TEST_ASSERT_TRUE(actual.has_value());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(*actual));
}

void test_labels_map_to_gestures() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::Two), static_cast<int>(gestureFromLabel("two")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::Like), static_cast<int>(gestureFromLabel("like")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::None), static_cast<int>(gestureFromLabel("no_hand")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::None), static_cast<int>(gestureFromLabel("no_gesture")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::None), static_cast<int>(gestureFromLabel(nullptr)));
}

void test_stored_gesture_values_round_trip() {
  for (int i = 0; i < heatboard::kGestureCount; ++i) {
    const auto gesture = gestureFromInt(i);
    TEST_ASSERT_TRUE(gesture.has_value());
    TEST_ASSERT_EQUAL_INT(i, static_cast<int>(*gesture));
  }
  TEST_ASSERT_FALSE(gestureFromInt(-1).has_value());
  TEST_ASSERT_FALSE(gestureFromInt(heatboard::kGestureCount).has_value());
}

void test_default_assignment() {
  const GestureSettings settings;
  assertAction(BoardAction::PreviousHeat, settings.actionOf(Gesture::One));
  assertAction(BoardAction::NextHeat, settings.actionOf(Gesture::Two));
  assertAction(BoardAction::StartStop, settings.actionOf(Gesture::Like));
  TEST_ASSERT_FALSE(settings.actionOf(Gesture::Five).has_value());
  TEST_ASSERT_FALSE(settings.actionOf(Gesture::None).has_value());
}

void test_fires_after_the_hold_time() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 0, settings).has_value());
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 400, settings).has_value());
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 799, settings).has_value());
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
}

void test_fires_only_once_while_the_hand_stays_up() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
  for (int64_t t = 900; t < 5000; t += 100) {
    TEST_ASSERT_FALSE(trigger.update(Gesture::Two, t, settings).has_value());
  }
}

void test_fires_again_after_the_hand_went_away() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
  // Gone for longer than the gap tolerance.
  TEST_ASSERT_FALSE(trigger.update(Gesture::None, 900, settings).has_value());
  TEST_ASSERT_FALSE(trigger.update(Gesture::None, 1400, settings).has_value());
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 2000, settings).has_value());
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 2800, settings));
}

void test_short_gap_does_not_restart_the_hold() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  trigger.update(Gesture::None, 300, settings);  // one missed frame
  trigger.update(Gesture::Two, 500, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
}

void test_long_gap_restarts_the_hold() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  trigger.update(Gesture::None, 100, settings);
  trigger.update(Gesture::None, 600, settings);  // beyond the gap tolerance
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 700, settings).has_value());
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 1400, settings).has_value());
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 1500, settings));
}

void test_changing_gesture_restarts_the_hold() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  trigger.update(Gesture::One, 500, settings);  // first sighting: not yet believed
  trigger.update(Gesture::One, 600, settings);  // second: the hold restarts here
  TEST_ASSERT_FALSE(trigger.update(Gesture::One, 1300, settings).has_value());
  assertAction(BoardAction::PreviousHeat, trigger.update(Gesture::One, 1400, settings));
}

void test_single_frame_of_another_gesture_is_ignored() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  trigger.update(Gesture::Three, 300, settings);  // one flickering frame
  trigger.update(Gesture::Two, 400, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
}

void test_flicker_through_none_does_not_restart() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  trigger.update(Gesture::Three, 200, settings);
  trigger.update(Gesture::None, 300, settings);
  trigger.update(Gesture::Two, 400, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
}

void test_unassigned_gesture_never_fires() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  for (int64_t t = 0; t <= 3000; t += 100) {
    TEST_ASSERT_FALSE(trigger.update(Gesture::Five, t, settings).has_value());
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, trigger.progress(3000, settings));
}

void test_cooldown_blocks_a_quick_second_trigger() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  assertAction(BoardAction::NextHeat, trigger.update(Gesture::Two, 800, settings));
  // Switch straight to another gesture and hold it: within the cooldown it must not fire.
  trigger.update(Gesture::One, 850, settings);
  trigger.update(Gesture::One, 900, settings);
  TEST_ASSERT_FALSE(trigger.update(Gesture::One, 1700, settings).has_value());
  // Held again afterwards, it does.
  trigger.update(Gesture::None, 1800, settings);
  trigger.update(Gesture::None, 2300, settings);
  trigger.update(Gesture::One, 2400, settings);
  assertAction(BoardAction::PreviousHeat, trigger.update(Gesture::One, 3200, settings));
}

void test_disabled_settings_never_fire_and_reset() {
  GestureTrigger trigger;
  auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  settings.enabled = false;
  TEST_ASSERT_FALSE(trigger.update(Gesture::Two, 800, settings).has_value());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::None), static_cast<int>(trigger.holding()));
}

void test_progress_follows_the_hold() {
  GestureTrigger trigger;
  const auto settings = enabledSettings();
  trigger.update(Gesture::Two, 0, settings);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::Two), static_cast<int>(trigger.holding()));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, trigger.progress(400, settings));
  trigger.update(Gesture::Two, 800, settings);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Gesture::None), static_cast<int>(trigger.holding()));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, trigger.progress(900, settings));
}

}  // namespace

void runGestureControlTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_labels_map_to_gestures);
  RUN_TEST(test_stored_gesture_values_round_trip);
  RUN_TEST(test_default_assignment);
  RUN_TEST(test_fires_after_the_hold_time);
  RUN_TEST(test_fires_only_once_while_the_hand_stays_up);
  RUN_TEST(test_fires_again_after_the_hand_went_away);
  RUN_TEST(test_short_gap_does_not_restart_the_hold);
  RUN_TEST(test_long_gap_restarts_the_hold);
  RUN_TEST(test_changing_gesture_restarts_the_hold);
  RUN_TEST(test_single_frame_of_another_gesture_is_ignored);
  RUN_TEST(test_flicker_through_none_does_not_restart);
  RUN_TEST(test_unassigned_gesture_never_fires);
  RUN_TEST(test_cooldown_blocks_a_quick_second_trigger);
  RUN_TEST(test_disabled_settings_never_fire_and_reset);
  RUN_TEST(test_progress_follows_the_hold);
}

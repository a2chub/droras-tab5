#include <unity.h>

#include "heatboard/orientation.h"
#include "test_suites.h"

using heatboard::Orientation;
using heatboard::OrientationDetector;
using heatboard::OrientationMode;
using heatboard::orientationModeFromInt;
using heatboard::resolveOrientation;

namespace {

constexpr int64_t kHold = OrientationDetector::kHoldMs;

void assertOrientation(Orientation expected, Orientation actual) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void test_starts_normal_without_a_reading() {
  const OrientationDetector detector;
  assertOrientation(Orientation::Normal, detector.orientation());
}

void test_first_clear_reading_is_adopted_immediately() {
  OrientationDetector detector;
  assertOrientation(Orientation::UpsideDown, detector.update(-1.0f, 0));
}

void test_lying_flat_at_start_stays_normal_until_tilted() {
  OrientationDetector detector;
  assertOrientation(Orientation::Normal, detector.update(0.1f, 0));
  assertOrientation(Orientation::UpsideDown, detector.update(-0.9f, 10));
}

void test_flips_after_holding_the_other_way() {
  OrientationDetector detector;
  detector.update(1.0f, 0);
  assertOrientation(Orientation::Normal, detector.update(-1.0f, 1000));
  assertOrientation(Orientation::Normal, detector.update(-1.0f, 1000 + kHold - 1));
  assertOrientation(Orientation::UpsideDown, detector.update(-1.0f, 1000 + kHold));
}

void test_short_jolt_does_not_flip() {
  OrientationDetector detector;
  detector.update(1.0f, 0);
  detector.update(-1.0f, 100);
  detector.update(1.0f, 200);  // back before the hold ran out
  // The hold starts again from here rather than from the first jolt.
  assertOrientation(Orientation::Normal, detector.update(-1.0f, 100 + kHold));
  assertOrientation(Orientation::Normal, detector.update(-1.0f, 100 + 2 * kHold - 1));
  assertOrientation(Orientation::UpsideDown, detector.update(-1.0f, 100 + 2 * kHold));
}

void test_lying_flat_keeps_the_last_orientation() {
  OrientationDetector detector;
  detector.update(-1.0f, 0);
  assertOrientation(Orientation::UpsideDown, detector.update(0.0f, 1000));
  assertOrientation(Orientation::UpsideDown, detector.update(0.3f, 1000 + 10 * kHold));
}

void test_passing_through_flat_resets_the_hold() {
  OrientationDetector detector;
  detector.update(1.0f, 0);
  detector.update(-1.0f, 100);
  detector.update(0.0f, 200);
  assertOrientation(Orientation::Normal, detector.update(-1.0f, 100 + kHold));
}

void test_readings_below_the_threshold_are_undecided() {
  OrientationDetector detector;
  detector.update(1.0f, 0);
  const float justBelow = -OrientationDetector::kThresholdG + 0.01f;
  detector.update(justBelow, 100);
  assertOrientation(Orientation::Normal, detector.update(justBelow, 100 + 10 * kHold));
}

void test_resolve_uses_the_sensor_only_in_auto() {
  assertOrientation(Orientation::UpsideDown, resolveOrientation(OrientationMode::Auto, Orientation::UpsideDown));
  assertOrientation(Orientation::Normal, resolveOrientation(OrientationMode::Auto, Orientation::Normal));
  assertOrientation(Orientation::Normal, resolveOrientation(OrientationMode::Normal, Orientation::UpsideDown));
  assertOrientation(Orientation::UpsideDown, resolveOrientation(OrientationMode::UpsideDown, Orientation::Normal));
}

void test_mode_round_trips_through_its_stored_value() {
  for (const OrientationMode mode : {OrientationMode::Auto, OrientationMode::Normal, OrientationMode::UpsideDown}) {
    const auto parsed = orientationModeFromInt(static_cast<int>(mode));
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(mode), static_cast<int>(*parsed));
  }
}

void test_unknown_stored_mode_is_rejected() {
  TEST_ASSERT_FALSE(orientationModeFromInt(-1).has_value());
  TEST_ASSERT_FALSE(orientationModeFromInt(3).has_value());
}

}  // namespace

void runOrientationTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_starts_normal_without_a_reading);
  RUN_TEST(test_first_clear_reading_is_adopted_immediately);
  RUN_TEST(test_lying_flat_at_start_stays_normal_until_tilted);
  RUN_TEST(test_flips_after_holding_the_other_way);
  RUN_TEST(test_short_jolt_does_not_flip);
  RUN_TEST(test_lying_flat_keeps_the_last_orientation);
  RUN_TEST(test_passing_through_flat_resets_the_hold);
  RUN_TEST(test_readings_below_the_threshold_are_undecided);
  RUN_TEST(test_resolve_uses_the_sensor_only_in_auto);
  RUN_TEST(test_mode_round_trips_through_its_stored_value);
  RUN_TEST(test_unknown_stored_mode_is_rejected);
}

#include <cmath>
#include <limits>

#include <unity.h>

#include "heatboard/server_heartbeat.h"
#include "test_suites.h"

namespace {

using heatboard::ServerHeartbeat;
using heatboard::ServerHeartbeatTiming;
constexpr ServerHeartbeatTiming kNormalTiming{25'000, 20'000};

void establish(ServerHeartbeat& health, int64_t atMs = 0) {
  health.begin(atMs);
  TEST_ASSERT_TRUE(health.engineOpened(kNormalTiming, atMs));
  TEST_ASSERT_TRUE(health.namespaceJoined(atMs));
}

void test_namespace_is_required_before_healthy() {
  ServerHeartbeat health;
  TEST_ASSERT_FALSE(health.isHealthy(0));
  health.begin(0);
  TEST_ASSERT_FALSE(health.isHealthy(0));
  TEST_ASSERT_TRUE(health.engineOpened(kNormalTiming, 100));
  TEST_ASSERT_TRUE(health.receivedPing(200));
  TEST_ASSERT_FALSE(health.isHealthy(200));
  TEST_ASSERT_TRUE(health.namespaceJoined(300));
  TEST_ASSERT_TRUE(health.isHealthy(300));
}

void test_missing_engine_open_times_out_at_ten_seconds() {
  ServerHeartbeat health;
  health.begin(100);
  TEST_ASSERT_FALSE(health.poll(10'099));
  TEST_ASSERT_TRUE(health.poll(10'100));
  TEST_ASSERT_FALSE(health.poll(10'101));
  TEST_ASSERT_FALSE(health.engineOpened(kNormalTiming, 10'101));
}

void test_pings_cannot_hide_a_missing_namespace_ack() {
  ServerHeartbeat health;
  health.begin(0);
  TEST_ASSERT_TRUE(health.engineOpened(kNormalTiming, 100));
  TEST_ASSERT_TRUE(health.receivedPing(10'000));
  TEST_ASSERT_FALSE(health.poll(10'099));
  TEST_ASSERT_TRUE(health.poll(10'100));
  TEST_ASSERT_FALSE(health.namespaceJoined(10'100));
}

void test_server_silence_expires_at_negotiated_sum() {
  ServerHeartbeat health;
  establish(health, 1'000);
  TEST_ASSERT_TRUE(health.isHealthy(45'999));
  // The command gate must reject immediately, even before the periodic poll runs.
  TEST_ASSERT_FALSE(health.isHealthy(46'000));
  TEST_ASSERT_TRUE(health.poll(46'000));
}

void test_engine_ping_refreshes_deadline() {
  ServerHeartbeat health;
  establish(health);
  TEST_ASSERT_TRUE(health.receivedPing(25'000));
  TEST_ASSERT_TRUE(health.isHealthy(69'999));
  TEST_ASSERT_FALSE(health.isHealthy(70'000));
}

void test_namespace_ack_does_not_extend_engine_deadline() {
  ServerHeartbeat health;
  health.begin(0);
  TEST_ASSERT_TRUE(health.engineOpened(kNormalTiming, 100));
  TEST_ASSERT_TRUE(health.namespaceJoined(9'000));
  TEST_ASSERT_TRUE(health.isHealthy(45'099));
  TEST_ASSERT_FALSE(health.isHealthy(45'100));
}

void test_late_ping_cannot_resurrect_expired_connection() {
  ServerHeartbeat health;
  establish(health);
  TEST_ASSERT_FALSE(health.receivedPing(45'000));
  TEST_ASSERT_FALSE(health.isHealthy(45'000));
  TEST_ASSERT_TRUE(health.poll(45'000));
}

void test_protocol_failure_requires_a_fresh_handshake() {
  ServerHeartbeat health;
  establish(health);
  health.fail();
  TEST_ASSERT_FALSE(health.isHealthy(1));
  TEST_ASSERT_FALSE(health.receivedPing(1));
  TEST_ASSERT_FALSE(health.namespaceJoined(1));
  establish(health, 3'000);
  TEST_ASSERT_TRUE(health.isHealthy(3'001));
}

void test_new_attempt_cannot_reuse_previous_namespace_or_heartbeat() {
  ServerHeartbeat health;
  establish(health);
  health.begin(5'000);
  TEST_ASSERT_FALSE(health.receivedPing(5'001));
  TEST_ASSERT_FALSE(health.namespaceJoined(5'002));
  TEST_ASSERT_FALSE(health.isHealthy(5'002));
  TEST_ASSERT_TRUE(health.poll(15'000));
}

void test_disabling_removes_pending_timeouts() {
  ServerHeartbeat health;
  establish(health);
  health.reset();
  TEST_ASSERT_FALSE(health.poll(1'000'000));
  TEST_ASSERT_FALSE(health.isHealthy(1'000'000));
  TEST_ASSERT_FALSE(health.receivedPing(1'000'000));
}

void test_small_negotiated_window_also_bounds_namespace_wait() {
  ServerHeartbeat health;
  health.begin(0);
  TEST_ASSERT_TRUE(health.engineOpened({100, 100}, 0));
  TEST_ASSERT_FALSE(health.poll(199));
  TEST_ASSERT_TRUE(health.poll(200));
}

void test_invalid_json_numbers_cannot_disable_monitoring() {
  for (const double bad : {0.0, -1.0, 0.5, 1.5, 120'001.0,
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::max()}) {
    TEST_ASSERT_FALSE(heatboard::serverHeartbeatTiming(bad, 1).has_value());
    TEST_ASSERT_FALSE(heatboard::serverHeartbeatTiming(1, bad).has_value());
  }
  TEST_ASSERT_FALSE(heatboard::serverHeartbeatTiming(60'000, 60'001).has_value());
  const auto maximum = heatboard::serverHeartbeatTiming(60'000, 60'000);
  TEST_ASSERT_TRUE(maximum.has_value());
  TEST_ASSERT_EQUAL_INT64(60'000, maximum->intervalMs);
  TEST_ASSERT_EQUAL_INT64(60'000, maximum->timeoutMs);
}

void test_unvalidated_timing_is_rejected_before_integer_addition() {
  ServerHeartbeat health;
  health.begin(0);
  TEST_ASSERT_FALSE(health.engineOpened({std::numeric_limits<int64_t>::max(), 1}, 0));
  TEST_ASSERT_FALSE(health.engineOpened({1, std::numeric_limits<int64_t>::max()}, 0));
  TEST_ASSERT_FALSE(health.engineOpened({-1, 100}, 0));
  TEST_ASSERT_FALSE(health.engineOpened({0, 100}, 0));
}

void test_large_monotonic_timestamps_do_not_overflow_deadlines() {
  ServerHeartbeat health;
  const int64_t maximum = std::numeric_limits<int64_t>::max();
  establish(health, maximum - 100);
  TEST_ASSERT_TRUE(health.isHealthy(maximum));
  TEST_ASSERT_FALSE(health.poll(maximum));
}

void test_clock_before_attempt_does_not_expire_early() {
  ServerHeartbeat health;
  establish(health, 1'000);
  TEST_ASSERT_TRUE(health.isHealthy(999));
  TEST_ASSERT_FALSE(health.poll(999));
}

}  // namespace

void runServerHeartbeatTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_namespace_is_required_before_healthy);
  RUN_TEST(test_missing_engine_open_times_out_at_ten_seconds);
  RUN_TEST(test_pings_cannot_hide_a_missing_namespace_ack);
  RUN_TEST(test_server_silence_expires_at_negotiated_sum);
  RUN_TEST(test_engine_ping_refreshes_deadline);
  RUN_TEST(test_namespace_ack_does_not_extend_engine_deadline);
  RUN_TEST(test_late_ping_cannot_resurrect_expired_connection);
  RUN_TEST(test_protocol_failure_requires_a_fresh_handshake);
  RUN_TEST(test_new_attempt_cannot_reuse_previous_namespace_or_heartbeat);
  RUN_TEST(test_disabling_removes_pending_timeouts);
  RUN_TEST(test_small_negotiated_window_also_bounds_namespace_wait);
  RUN_TEST(test_invalid_json_numbers_cannot_disable_monitoring);
  RUN_TEST(test_unvalidated_timing_is_rejected_before_integer_addition);
  RUN_TEST(test_large_monotonic_timestamps_do_not_overflow_deadlines);
  RUN_TEST(test_clock_before_attempt_does_not_expire_early);
}

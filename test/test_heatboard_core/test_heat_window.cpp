#include <initializer_list>
#include <optional>
#include <vector>

#include <unity.h>

#include "heatboard/heat_window.h"
#include "test_suites.h"

using heatboard::computeHeatWindow;
using heatboard::Heat;
using heatboard::HeatWindow;
using heatboard::nextHeatNumber;
using heatboard::previousHeatNumber;

namespace {

std::vector<Heat> heatsNumbered(std::initializer_list<int> numbers) {
  std::vector<Heat> heats;
  for (const int number : numbers) {
    Heat heat;
    heat.number = number;
    heats.push_back(heat);
  }
  return heats;
}

void assertWindow(const std::optional<HeatWindow>& window, std::size_t previous, std::size_t current,
                  std::size_t next, std::size_t afterNext) {
  TEST_ASSERT_TRUE(window.has_value());
  TEST_ASSERT_EQUAL_size_t(previous, window->previous);
  TEST_ASSERT_EQUAL_size_t(current, window->current);
  TEST_ASSERT_EQUAL_size_t(next, window->next);
  TEST_ASSERT_EQUAL_size_t(afterNext, window->afterNext);
}

void assertNumber(int expected, const std::optional<int>& actual) {
  TEST_ASSERT_TRUE(actual.has_value());
  TEST_ASSERT_EQUAL_INT(expected, *actual);
}

void test_window_middle_of_list() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2, 3, 4, 5, 6}), 3), 1, 2, 3, 4);
}

void test_window_first_heat_wraps_previous_to_last() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2, 3, 4, 5, 6}), 1), 5, 0, 1, 2);
}

void test_window_last_heat_wraps_next_and_after_next() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2, 3, 4, 5, 6}), 6), 4, 5, 0, 1);
}

void test_window_second_to_last_wraps_after_next() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2, 3, 4, 5, 6}), 5), 3, 4, 5, 0);
}

void test_window_single_heat() {
  assertWindow(computeHeatWindow(heatsNumbered({7}), 7), 0, 0, 0, 0);
}

void test_window_two_heats() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2}), 1), 1, 0, 1, 0);
  assertWindow(computeHeatWindow(heatsNumbered({1, 2}), 2), 0, 1, 0, 1);
}

void test_window_three_heats() {
  assertWindow(computeHeatWindow(heatsNumbered({1, 2, 3}), 2), 0, 1, 2, 0);
}

void test_window_looks_up_by_number_not_index() {
  // Heat numbers need not be contiguous or start at 1.
  assertWindow(computeHeatWindow(heatsNumbered({4, 5, 7, 23, 38}), 7), 1, 2, 3, 4);
  assertWindow(computeHeatWindow(heatsNumbered({4, 5, 7, 23, 38}), 4), 4, 0, 1, 2);
}

void test_window_unknown_number() {
  const std::vector<Heat> heats = heatsNumbered({1, 2, 3});
  TEST_ASSERT_FALSE(computeHeatWindow(heats, 4).has_value());
  TEST_ASSERT_FALSE(computeHeatWindow(heats, 0).has_value());
  TEST_ASSERT_FALSE(computeHeatWindow(heats, -1).has_value());
}

void test_window_empty_list() {
  TEST_ASSERT_FALSE(computeHeatWindow({}, 1).has_value());
}

void test_next_heat_number() {
  const std::vector<Heat> heats = heatsNumbered({4, 5, 7, 23, 38});
  assertNumber(5, nextHeatNumber(heats, 4));
  assertNumber(23, nextHeatNumber(heats, 7));
  assertNumber(4, nextHeatNumber(heats, 38));
}

void test_previous_heat_number() {
  const std::vector<Heat> heats = heatsNumbered({4, 5, 7, 23, 38});
  assertNumber(38, previousHeatNumber(heats, 4));
  assertNumber(5, previousHeatNumber(heats, 7));
  assertNumber(23, previousHeatNumber(heats, 38));
}

void test_next_previous_single_heat() {
  const std::vector<Heat> heats = heatsNumbered({7});
  assertNumber(7, nextHeatNumber(heats, 7));
  assertNumber(7, previousHeatNumber(heats, 7));
}

void test_next_previous_unknown_or_empty() {
  const std::vector<Heat> heats = heatsNumbered({1, 2, 3});
  TEST_ASSERT_FALSE(nextHeatNumber(heats, 9).has_value());
  TEST_ASSERT_FALSE(previousHeatNumber(heats, 9).has_value());
  TEST_ASSERT_FALSE(nextHeatNumber({}, 1).has_value());
  TEST_ASSERT_FALSE(previousHeatNumber({}, 1).has_value());
}

}  // namespace

void runHeatWindowTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_window_middle_of_list);
  RUN_TEST(test_window_first_heat_wraps_previous_to_last);
  RUN_TEST(test_window_last_heat_wraps_next_and_after_next);
  RUN_TEST(test_window_second_to_last_wraps_after_next);
  RUN_TEST(test_window_single_heat);
  RUN_TEST(test_window_two_heats);
  RUN_TEST(test_window_three_heats);
  RUN_TEST(test_window_looks_up_by_number_not_index);
  RUN_TEST(test_window_unknown_number);
  RUN_TEST(test_window_empty_list);
  RUN_TEST(test_next_heat_number);
  RUN_TEST(test_previous_heat_number);
  RUN_TEST(test_next_previous_single_heat);
  RUN_TEST(test_next_previous_unknown_or_empty);
}

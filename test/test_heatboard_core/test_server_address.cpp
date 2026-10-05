#include <optional>
#include <string>
#include <string_view>

#include <unity.h>

#include "heatboard/server_address.h"
#include "test_suites.h"

using heatboard::formatServerAddress;
using heatboard::parseServerAddress;
using heatboard::ServerAddress;

namespace {

void assertAddress(std::string_view text, const char* host, unsigned port) {
  const std::optional<ServerAddress> address = parseServerAddress(text);
  const std::string label(text);
  TEST_ASSERT_TRUE_MESSAGE(address.has_value(), label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(host, address->host.c_str(), label.c_str());
  TEST_ASSERT_EQUAL_UINT_MESSAGE(port, address->port, label.c_str());
}

void assertRejected(std::string_view text) {
  const std::string label = "'" + std::string(text) + "'";
  TEST_ASSERT_FALSE_MESSAGE(parseServerAddress(text).has_value(), label.c_str());
}

void test_address_default_port_constant() {
  TEST_ASSERT_EQUAL_UINT(8000, heatboard::kDefaultServerPort);
}

void test_address_without_port_uses_default() {
  assertAddress("192.168.1.10", "192.168.1.10", 8000);
}

void test_address_without_port_uses_given_default() {
  const std::optional<ServerAddress> address = parseServerAddress("10.0.0.1", 5000);
  TEST_ASSERT_TRUE(address.has_value());
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", address->host.c_str());
  TEST_ASSERT_EQUAL_UINT(5000, address->port);
}

void test_address_with_port() {
  assertAddress("192.168.1.10:8080", "192.168.1.10", 8080);
}

void test_address_explicit_port_overrides_default() {
  const std::optional<ServerAddress> address = parseServerAddress("10.0.0.1:81", 5000);
  TEST_ASSERT_TRUE(address.has_value());
  TEST_ASSERT_EQUAL_UINT(81, address->port);
}

void test_address_range_limits() {
  assertAddress("0.0.0.0:1", "0.0.0.0", 1);
  assertAddress("255.255.255.255:65535", "255.255.255.255", 65535);
}

void test_address_surrounding_whitespace() {
  assertAddress("  192.168.1.10:8000\n", "192.168.1.10", 8000);
  assertAddress("\t10.0.0.1 ", "10.0.0.1", 8000);
}

void test_address_leading_zeros_normalised() {
  assertAddress("192.168.001.010", "192.168.1.10", 8000);
  assertAddress("010.000.00.1:08000", "10.0.0.1", 8000);
}

void test_address_rejects_empty() {
  assertRejected("");
  assertRejected("   ");
}

void test_address_rejects_wrong_octet_count() {
  assertRejected("192.168.1");
  assertRejected("192.168.1.10.5");
  assertRejected("192");
  assertRejected("192.168.1:8000");
}

void test_address_rejects_octet_out_of_range() {
  assertRejected("192.168.1.256");
  assertRejected("256.168.1.1");
  assertRejected("192.168.1.99999999999");
  assertRejected("192.168.1.-1");
}

void test_address_rejects_port_out_of_range() {
  assertRejected("192.168.1.10:0");
  assertRejected("192.168.1.10:65536");
  assertRejected("192.168.1.10:99999999999");
  assertRejected("192.168.1.10:-1");
}

void test_address_rejects_letters() {
  assertRejected("raspberrypi.local");
  assertRejected("192.168.1.a");
  assertRejected("192.168.1.10:http");
  assertRejected("192.168.1.10:80a");
  assertRejected("0x10.1.1.1");
}

void test_address_rejects_trailing_colon() {
  assertRejected("192.168.1.10:");
}

void test_address_rejects_double_dots() {
  assertRejected("192..168.1");
  assertRejected("192.168..1.10");
  assertRejected("192.168.1.10.");
  assertRejected(".192.168.1.10");
}

void test_address_rejects_stray_separators() {
  assertRejected("192.168.1.10:80:80");
  assertRejected(":8000");
  assertRejected("192.168.1. 10");
  assertRejected("192.168.1.10 :8000");
  assertRejected("192.168.1.10: 8000");
  assertRejected("+192.168.1.10");
}

void test_address_format() {
  ServerAddress address;
  address.host = "192.168.1.10";
  address.port = 8000;
  TEST_ASSERT_EQUAL_STRING("192.168.1.10:8000", formatServerAddress(address).c_str());
}

void test_address_format_round_trips_through_parse() {
  const std::optional<ServerAddress> parsed = parseServerAddress(" 010.0.0.001:65535 ");
  TEST_ASSERT_TRUE(parsed.has_value());
  const std::string formatted = formatServerAddress(*parsed);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1:65535", formatted.c_str());

  const std::optional<ServerAddress> reparsed = parseServerAddress(formatted);
  TEST_ASSERT_TRUE(reparsed.has_value());
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", reparsed->host.c_str());
  TEST_ASSERT_EQUAL_UINT(65535, reparsed->port);
}

}  // namespace

void runServerAddressTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_address_default_port_constant);
  RUN_TEST(test_address_without_port_uses_default);
  RUN_TEST(test_address_without_port_uses_given_default);
  RUN_TEST(test_address_with_port);
  RUN_TEST(test_address_explicit_port_overrides_default);
  RUN_TEST(test_address_range_limits);
  RUN_TEST(test_address_surrounding_whitespace);
  RUN_TEST(test_address_leading_zeros_normalised);
  RUN_TEST(test_address_rejects_empty);
  RUN_TEST(test_address_rejects_wrong_octet_count);
  RUN_TEST(test_address_rejects_octet_out_of_range);
  RUN_TEST(test_address_rejects_port_out_of_range);
  RUN_TEST(test_address_rejects_letters);
  RUN_TEST(test_address_rejects_trailing_colon);
  RUN_TEST(test_address_rejects_double_dots);
  RUN_TEST(test_address_rejects_stray_separators);
  RUN_TEST(test_address_format);
  RUN_TEST(test_address_format_round_trips_through_parse);
}

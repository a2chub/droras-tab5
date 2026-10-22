#include <unity.h>

#include "heatboard/wifi_profile.h"
#include "test_suites.h"

#include <string>
#include <vector>

namespace {

using heatboard::WifiProfile;
using heatboard::WifiProfileError;
using heatboard::decodeWifiProfile;
using heatboard::encodeWifiProfile;
using heatboard::validateWifiProfile;

void assertError(WifiProfileError expected, const WifiProfile& profile) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(validateWifiProfile(profile)));
}

void assertRoundTrip(const WifiProfile& profile) {
  const auto bytes = encodeWifiProfile(profile);
  TEST_ASSERT_TRUE(bytes.has_value());
  const auto decoded = decodeWifiProfile(bytes->data(), bytes->size());
  TEST_ASSERT_TRUE(decoded.has_value());
  TEST_ASSERT_TRUE(decoded->ssid == profile.ssid);
  TEST_ASSERT_TRUE(decoded->password == profile.password);
}

void test_wifi_ssid_byte_boundaries() {
  assertError(WifiProfileError::EmptySsid, {"", ""});
  assertError(WifiProfileError::None, {"a", ""});
  assertError(WifiProfileError::None, {std::string(32, 's'), ""});
  assertError(WifiProfileError::SsidTooLong, {std::string(33, 's'), ""});
}

void test_wifi_ssid_rejects_every_ascii_control_character() {
  for (int value = 0; value < 32; ++value) {
    const std::string ssid = std::string("ssid") + static_cast<char>(value) + "name";
    assertError(WifiProfileError::InvalidSsid, {ssid, ""});
  }
  assertError(WifiProfileError::InvalidSsid, {std::string("ssid") + '\x7f', ""});
}

void test_wifi_ssid_unicode_uses_byte_limit_and_preserves_spaces() {
  std::string ssid;
  for (int i = 0; i < 10; ++i) ssid += "あ";
  TEST_ASSERT_EQUAL_UINT(30, ssid.size());
  assertError(WifiProfileError::None, {ssid + "12", ""});
  assertError(WifiProfileError::SsidTooLong, {ssid + "あ", ""});
  assertRoundTrip({"  日本語 Wi-Fi  ", ""});
  assertRoundTrip({" ", ""});
}

void test_wifi_password_length_boundaries_and_open_network() {
  assertError(WifiProfileError::None, {"network", ""});
  assertError(WifiProfileError::InvalidPassword, {"network", "1"});
  assertError(WifiProfileError::InvalidPassword, {"network", "1234567"});
  assertError(WifiProfileError::None, {"network", "12345678"});
  assertError(WifiProfileError::None, {"network", std::string(63, 'p')});
  assertError(WifiProfileError::InvalidPassword, {"network", std::string(64, 'p')});
  assertError(WifiProfileError::InvalidPassword, {"network", std::string(65, 'a')});
}

void test_wifi_password_requires_printable_ascii() {
  for (int value = 0; value <= 255; ++value) {
    std::string password(8, 'x');
    password[3] = static_cast<char>(value);
    assertError(value >= 0x20 && value <= 0x7e ? WifiProfileError::None : WifiProfileError::InvalidPassword,
                {"network", password});
  }
  assertError(WifiProfileError::InvalidPassword, {"network", "あいう"});
  assertRoundTrip({"network", "  abcd  "});
}

void test_wifi_password_accepts_only_hex_at_64_bytes() {
  std::string psk;
  for (int i = 0; i < 4; ++i) psk += "0123456789aBcDeF";
  assertError(WifiProfileError::None, {"network", psk});
  assertRoundTrip({std::string(32, 's'), psk});
  for (char invalid : {'g', 'G', ' ', '-', '\0', '\n'}) {
    auto badPsk = psk;
    badPsk[31] = invalid;
    assertError(WifiProfileError::InvalidPassword, {"network", badPsk});
  }
}

void test_wifi_codec_round_trips_each_supported_password_form() {
  assertRoundTrip({"Open network", ""});
  assertRoundTrip({"Protected network", "dummy passphrase!"});
  assertRoundTrip({std::string(32, 's'), std::string(63, 'p')});
  assertRoundTrip({std::string(32, 's'), std::string(64, 'a')});
  const auto largest = encodeWifiProfile({std::string(32, 's'), std::string(64, 'a')});
  TEST_ASSERT_TRUE(largest.has_value());
  TEST_ASSERT_EQUAL_UINT(heatboard::kWifiProfileBlobMaxBytes, largest->size());
}

void test_wifi_codec_refuses_invalid_profiles() {
  TEST_ASSERT_FALSE(encodeWifiProfile({"", "12345678"}).has_value());
  TEST_ASSERT_FALSE(encodeWifiProfile({std::string(33, 's'), ""}).has_value());
  TEST_ASSERT_FALSE(encodeWifiProfile({"bad\nssid", ""}).has_value());
  TEST_ASSERT_FALSE(encodeWifiProfile({"network", "short"}).has_value());
}

void test_wifi_codec_rejects_truncated_and_extra_bytes() {
  const auto bytes = encodeWifiProfile({"Test network", "dummy password"});
  TEST_ASSERT_TRUE(bytes.has_value());
  for (std::size_t size = 0; size < bytes->size(); ++size) {
    TEST_ASSERT_FALSE(decodeWifiProfile(bytes->data(), size).has_value());
  }
  auto extended = *bytes;
  extended.push_back(0);
  TEST_ASSERT_FALSE(decodeWifiProfile(extended.data(), extended.size()).has_value());
  extended.resize(heatboard::kWifiProfileBlobMaxBytes + 1);
  TEST_ASSERT_FALSE(decodeWifiProfile(extended.data(), extended.size()).has_value());
  TEST_ASSERT_FALSE(decodeWifiProfile(nullptr, 0).has_value());
  TEST_ASSERT_FALSE(decodeWifiProfile(nullptr, bytes->size()).has_value());
}

void test_wifi_codec_rejects_unknown_version_and_magic() {
  const auto bytes = encodeWifiProfile({"network", "12345678"});
  TEST_ASSERT_TRUE(bytes.has_value());
  for (std::size_t index = 0; index < 3; ++index) {
    auto corrupt = *bytes;
    corrupt[index] ^= 0xff;
    TEST_ASSERT_FALSE(decodeWifiProfile(corrupt.data(), corrupt.size()).has_value());
  }
}

void test_wifi_codec_rejects_invalid_payload_lengths_and_content() {
  const auto bytes = encodeWifiProfile({"network", "12345678"});
  TEST_ASSERT_TRUE(bytes.has_value());
  for (std::size_t index : {std::size_t{3}, std::size_t{4}}) {
    auto corrupt = *bytes;
    corrupt[index] = 255;
    TEST_ASSERT_FALSE(decodeWifiProfile(corrupt.data(), corrupt.size()).has_value());
  }
  auto corruptSsid = *bytes;
  corruptSsid[5] = 0;
  TEST_ASSERT_FALSE(decodeWifiProfile(corruptSsid.data(), corruptSsid.size()).has_value());
  auto corruptPassword = *bytes;
  corruptPassword.back() = 0x80;
  TEST_ASSERT_FALSE(decodeWifiProfile(corruptPassword.data(), corruptPassword.size()).has_value());
  std::vector<uint8_t> emptySsid{'W', 'F', 1, 0, 0};
  TEST_ASSERT_FALSE(decodeWifiProfile(emptySsid.data(), emptySsid.size()).has_value());
}

}  // namespace

void runWifiProfileTests() {
  UnitySetTestFile(__FILE__);
  RUN_TEST(test_wifi_ssid_byte_boundaries);
  RUN_TEST(test_wifi_ssid_rejects_every_ascii_control_character);
  RUN_TEST(test_wifi_ssid_unicode_uses_byte_limit_and_preserves_spaces);
  RUN_TEST(test_wifi_password_length_boundaries_and_open_network);
  RUN_TEST(test_wifi_password_requires_printable_ascii);
  RUN_TEST(test_wifi_password_accepts_only_hex_at_64_bytes);
  RUN_TEST(test_wifi_codec_round_trips_each_supported_password_form);
  RUN_TEST(test_wifi_codec_refuses_invalid_profiles);
  RUN_TEST(test_wifi_codec_rejects_truncated_and_extra_bytes);
  RUN_TEST(test_wifi_codec_rejects_unknown_version_and_magic);
  RUN_TEST(test_wifi_codec_rejects_invalid_payload_lengths_and_content);
}

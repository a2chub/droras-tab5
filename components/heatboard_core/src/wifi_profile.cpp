#include "heatboard/wifi_profile.h"

#include <algorithm>

namespace heatboard {
namespace {

constexpr uint8_t kBlobVersion = 1;
constexpr std::size_t kBlobHeaderBytes = 5;

bool isPrintableAscii(unsigned char value) { return value >= 0x20 && value <= 0x7e; }

bool isHexDigit(unsigned char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

}  // namespace

WifiProfileError validateWifiProfile(const WifiProfile& profile) {
  if (profile.ssid.empty()) return WifiProfileError::EmptySsid;
  if (profile.ssid.size() > kWifiSsidMaxBytes) return WifiProfileError::SsidTooLong;
  for (unsigned char value : profile.ssid) {
    if (value < 0x20 || value == 0x7f) return WifiProfileError::InvalidSsid;
  }

  const auto& password = profile.password;
  if (password.empty()) return WifiProfileError::None;
  if (password.size() == kWifiPasswordMaxBytes &&
      std::all_of(password.begin(), password.end(), isHexDigit)) {
    return WifiProfileError::None;
  }
  if (password.size() < 8 || password.size() > 63 ||
      !std::all_of(password.begin(), password.end(), isPrintableAscii)) {
    return WifiProfileError::InvalidPassword;
  }
  return WifiProfileError::None;
}

std::optional<std::vector<uint8_t>> encodeWifiProfile(const WifiProfile& profile) {
  if (validateWifiProfile(profile) != WifiProfileError::None) return std::nullopt;
  std::vector<uint8_t> data;
  data.reserve(kBlobHeaderBytes + profile.ssid.size() + profile.password.size());
  data.insert(data.end(), {'W', 'F', kBlobVersion, static_cast<uint8_t>(profile.ssid.size()),
                           static_cast<uint8_t>(profile.password.size())});
  data.insert(data.end(), profile.ssid.begin(), profile.ssid.end());
  data.insert(data.end(), profile.password.begin(), profile.password.end());
  return data;
}

std::optional<WifiProfile> decodeWifiProfile(const uint8_t* data, std::size_t size) {
  if (data == nullptr || size < kBlobHeaderBytes || size > kWifiProfileBlobMaxBytes ||
      data[0] != 'W' || data[1] != 'F' || data[2] != kBlobVersion) {
    return std::nullopt;
  }
  const std::size_t ssidSize = data[3];
  const std::size_t passwordSize = data[4];
  if (ssidSize > kWifiSsidMaxBytes || passwordSize > kWifiPasswordMaxBytes ||
      size != kBlobHeaderBytes + ssidSize + passwordSize) {
    return std::nullopt;
  }
  WifiProfile profile{
      std::string(reinterpret_cast<const char*>(data + kBlobHeaderBytes), ssidSize),
      std::string(reinterpret_cast<const char*>(data + kBlobHeaderBytes + ssidSize), passwordSize)};
  if (validateWifiProfile(profile) != WifiProfileError::None) return std::nullopt;
  return profile;
}

}  // namespace heatboard

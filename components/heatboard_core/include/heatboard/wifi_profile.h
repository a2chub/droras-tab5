#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace heatboard {

inline constexpr int kWifiNetworkSlots = 4;
inline constexpr int kWifiSsidMaxBytes = 32;
inline constexpr int kWifiPasswordMaxBytes = 64;

struct WifiProfile {
  std::string ssid;
  std::string password;
};

enum class WifiProfileError { None, EmptySsid, SsidTooLong, InvalidSsid, InvalidPassword };

// SSIDs retain whitespace and non-ASCII bytes. Passwords are empty for open
// networks, 8-63 printable ASCII bytes, or a 64-digit hexadecimal PSK.
WifiProfileError validateWifiProfile(const WifiProfile& profile);

// One versioned record keeps SSID and password together in a single NVS value.
inline constexpr std::size_t kWifiProfileBlobMaxBytes = 5 + kWifiSsidMaxBytes + kWifiPasswordMaxBytes;
std::optional<std::vector<uint8_t>> encodeWifiProfile(const WifiProfile& profile);
std::optional<WifiProfile> decodeWifiProfile(const uint8_t* data, std::size_t size);

}  // namespace heatboard

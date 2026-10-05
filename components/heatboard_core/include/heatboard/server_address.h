// Address of the droras server as typed by the operator.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace heatboard {

struct ServerAddress {
  std::string host;  // dotted IPv4, normalised (no leading zeros: "192.168.001.010" -> "192.168.1.10")
  std::uint16_t port = 0;
};

inline constexpr std::uint16_t kDefaultServerPort = 8000;

// Accepts "A.B.C.D" or "A.B.C.D:PORT" (surrounding ASCII whitespace ignored). Octets 0-255,
// port 1-65535. The text comes from an on-screen numeric keypad, so only IPv4 literals are
// supported — no hostnames. nullopt on anything else.
std::optional<ServerAddress> parseServerAddress(std::string_view text,
                                                std::uint16_t defaultPort = kDefaultServerPort);

std::string formatServerAddress(const ServerAddress& address);  // always "host:port"

}  // namespace heatboard

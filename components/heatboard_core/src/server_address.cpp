#include "heatboard/server_address.h"

#include <cstddef>

#include "text_util.h"

namespace heatboard {
namespace {

constexpr int kOctetCount = 4;
constexpr unsigned kMaxOctet = 255;
constexpr unsigned kMaxPort = 65535;

}  // namespace

std::optional<ServerAddress> parseServerAddress(std::string_view text, std::uint16_t defaultPort) {
  text = detail::trimAscii(text);

  ServerAddress address;
  address.port = defaultPort;

  const std::size_t colon = text.find(':');
  if (colon != std::string_view::npos) {
    const std::optional<unsigned> port = detail::parseWholeInteger<unsigned>(text.substr(colon + 1));
    if (!port || *port < 1 || *port > kMaxPort) {
      return std::nullopt;
    }
    address.port = static_cast<std::uint16_t>(*port);
    text = text.substr(0, colon);
  }

  for (int index = 0; index < kOctetCount; ++index) {
    const bool isLast = index == kOctetCount - 1;
    const std::size_t dot = text.find('.');
    if ((dot == std::string_view::npos) != isLast) {
      return std::nullopt;  // too few or too many octets
    }
    const std::optional<unsigned> octet = detail::parseWholeInteger<unsigned>(text.substr(0, dot));
    if (!octet || *octet > kMaxOctet) {
      return std::nullopt;
    }
    if (index > 0) {
      address.host += '.';
    }
    // Re-printing the parsed value is what drops leading zeros.
    address.host += std::to_string(*octet);
    text.remove_prefix(isLast ? text.size() : dot + 1);
  }
  return address;
}

std::string formatServerAddress(const ServerAddress& address) {
  return address.host + ":" + std::to_string(address.port);
}

}  // namespace heatboard

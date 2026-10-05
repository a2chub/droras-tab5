// Text helpers shared by the parsers in this component. Not part of the public API.
#pragma once

#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>

namespace heatboard::detail {

// std::isspace is avoided: it depends on the C locale and is undefined for the negative
// char values that UTF-8 bytes produce.
inline bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

inline std::string_view trimAscii(std::string_view text) {
  while (!text.empty() && isAsciiSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isAsciiSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

// Decimal integer spanning the whole of `text`. std::from_chars works without exceptions
// (the firmware builds with -fno-exceptions), reports overflow, and accepts neither leading
// whitespace nor '+', so partial matches such as "5 " or "5.0" are rejected by the end check.
template <typename Integer>
std::optional<Integer> parseWholeInteger(std::string_view text) {
  Integer value{};
  const char* const end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc() || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

}  // namespace heatboard::detail

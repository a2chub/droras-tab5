#include "heatboard/socketio_codec.h"

#include <cstddef>

#include "text_util.h"

namespace heatboard {
namespace {

SioPacket packetOfType(SioPacketType type) {
  SioPacket packet;
  packet.type = type;
  return packet;
}

// Connect and connect-error packets carry either nothing or a JSON object. Anything else in
// that position (notably "/ns,") means the packet is not for the default namespace.
bool isEmptyOrObject(std::string_view payload) {
  return payload.empty() || payload.front() == '{';
}

// Reads the JSON string at the front of `text` into `value` and removes it from `text`.
// Only the escapes an identifier-like event name can plausibly need are understood; any
// other escape (\n, \uXXXX, ...) fails so that a frame is never dispatched under a name
// that was decoded wrongly.
bool consumeJsonString(std::string_view& text, std::string& value) {
  if (text.empty() || text.front() != '"') {
    return false;
  }
  for (std::size_t pos = 1; pos < text.size(); ++pos) {
    char c = text[pos];
    if (c == '"') {
      text.remove_prefix(pos + 1);
      return true;
    }
    if (c == '\\') {
      if (++pos == text.size()) {
        return false;
      }
      c = text[pos];
      if (c != '"' && c != '\\' && c != '/') {
        return false;
      }
    }
    value += c;
  }
  return false;
}

// `body` is what follows "42".
SioPacket decodeEvent(std::string_view body) {
  while (!body.empty() && body.front() >= '0' && body.front() <= '9') {
    body.remove_prefix(1);  // ack id: this client never acknowledges
  }
  if (body.size() < 2 || body.front() != '[' || body.back() != ']') {
    return {};
  }
  body = detail::trimAscii(body.substr(1, body.size() - 2));

  SioPacket packet;
  if (!consumeJsonString(body, packet.eventName)) {
    return {};
  }
  body = detail::trimAscii(body);
  if (!body.empty()) {
    if (body.front() != ',') {
      return {};
    }
    body = detail::trimAscii(body.substr(1));
    if (body.empty()) {
      return {};
    }
    packet.argsJson = std::string(body);
  }
  packet.type = SioPacketType::Event;
  return packet;
}

// `payload` is what follows the Engine.IO message type "4", i.e. a Socket.IO packet.
SioPacket decodeSocketIoPacket(std::string_view payload) {
  if (payload.empty()) {
    return {};
  }
  const std::string_view body = payload.substr(1);
  switch (payload.front()) {
    case '0':
      return isEmptyOrObject(body) ? packetOfType(SioPacketType::NamespaceConnected) : SioPacket{};
    case '1':
      return body.empty() ? packetOfType(SioPacketType::NamespaceDisconnected) : SioPacket{};
    case '2':
      return decodeEvent(body);
    case '4':
      return isEmptyOrObject(body) ? packetOfType(SioPacketType::ConnectError) : SioPacket{};
    default:
      return {};
  }
}

std::string quoteJson(std::string_view text) {
  std::string quoted = "\"";
  for (const char c : text) {
    if (c == '"' || c == '\\') {
      quoted += '\\';
    }
    quoted += c;
  }
  quoted += '"';
  return quoted;
}

}  // namespace

SioPacket decodeSioPacket(std::string_view frame) {
  if (frame.empty()) {
    return {};
  }
  const std::string_view payload = frame.substr(1);
  switch (frame.front()) {
    case '0':
      return !payload.empty() && payload.front() == '{' ? packetOfType(SioPacketType::EngineOpen) : SioPacket{};
    case '1':
      return payload.empty() ? packetOfType(SioPacketType::EngineClose) : SioPacket{};
    case '2':
      return packetOfType(SioPacketType::EnginePing);
    case '3':
      return payload.empty() ? packetOfType(SioPacketType::EnginePong) : SioPacket{};
    case '4':
      return decodeSocketIoPacket(payload);
    default:
      return {};
  }
}

std::string encodeSioEvent(std::string_view eventName) {
  return "42[" + quoteJson(eventName) + "]";
}

std::string encodeSioEvent(std::string_view eventName, int value) {
  return "42[" + quoteJson(eventName) + "," + std::to_string(value) + "]";
}

std::optional<int> parseSioIntArg(std::string_view argsJson) {
  std::string_view text = detail::trimAscii(argsJson);
  if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
    text = text.substr(1, text.size() - 2);
  }
  return detail::parseWholeInteger<int>(text);
}

}  // namespace heatboard

// Text frames of Engine.IO v4 carrying Socket.IO v5, default namespace only.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace heatboard {

enum class SioPacketType {
  EngineOpen,             // "0{...}"
  EngineClose,            // "1"
  EnginePing,             // "2" (optionally followed by data)
  EnginePong,             // "3"
  NamespaceConnected,     // "40" or "40{...}"
  NamespaceDisconnected,  // "41"
  Event,                  // "42[...]" (an ack id between "42" and "[" is tolerated and ignored)
  ConnectError,           // "44" or "44{...}"
  Unknown,                // anything else, including malformed events and non-default namespaces ("42/ns,[...]")
};

struct SioPacket {
  SioPacketType type = SioPacketType::Unknown;
  std::string eventName;  // Event only
  std::string argsJson;   // Event only: raw JSON text of the arguments after the name, without the enclosing
                          // brackets and without the separating comma. `42["current_heat",5]` -> "5";
                          // `42["heat_list",[["a","b"]]]` -> `[["a","b"]]`; `42["start_heat"]` -> "".
};

// The event name must be a JSON string using no escapes other than \" \\ and \/ (the server's
// event names are plain ASCII identifiers); otherwise the frame is Unknown. `argsJson` is
// passed through verbatim and is not validated as JSON.
SioPacket decodeSioPacket(std::string_view frame);

inline constexpr std::string_view kSioNamespaceConnect = "40";  // sent by the client after EngineOpen
inline constexpr std::string_view kSioEnginePong = "3";         // reply to EnginePing

// Only `"` and `\` in the name are escaped; callers pass fixed ASCII identifiers.
std::string encodeSioEvent(std::string_view eventName);             // -> 42["start_heat"]
std::string encodeSioEvent(std::string_view eventName, int value);  // -> 42["set_current_heat",5]

// Reads a single integer argument. The server emits a JSON number, but a quoted number ("5")
// is accepted too. Surrounding whitespace is allowed; anything else (empty, floats, arrays,
// trailing garbage, overflow) yields nullopt.
std::optional<int> parseSioIntArg(std::string_view argsJson);

}  // namespace heatboard

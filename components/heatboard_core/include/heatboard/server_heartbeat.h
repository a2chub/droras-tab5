// Engine.IO v4 liveness, independent of sockets and the device clock.
#pragma once

#include <cstdint>
#include <optional>

namespace heatboard {

inline constexpr int64_t kServerHandshakeTimeoutMs = 10'000;
inline constexpr int64_t kServerHeartbeatMaxWindowMs = 120'000;

struct ServerHeartbeatTiming {
  int64_t intervalMs = 0;
  int64_t timeoutMs = 0;
};

// JSON numbers must be finite positive integers. Bound the combined window before
// converting to integers, so hostile/overflowing values cannot disable monitoring.
std::optional<ServerHeartbeatTiming> serverHeartbeatTiming(double intervalMs, double timeoutMs);

class ServerHeartbeat {
 public:
  void reset();
  void begin(int64_t nowMs);
  bool engineOpened(ServerHeartbeatTiming timing, int64_t nowMs);
  bool namespaceJoined(int64_t nowMs);
  bool receivedPing(int64_t nowMs);
  void fail();

  // Returns true only when this call transitions an active connection to failed.
  bool poll(int64_t nowMs);
  bool isHealthy(int64_t nowMs) const;

 private:
  enum class Phase { Idle, AwaitingOpen, AwaitingNamespace, Healthy, Failed };
  bool expired(int64_t nowMs) const;

  Phase phase_ = Phase::Idle;
  ServerHeartbeatTiming timing_;
  int64_t phaseStartedMs_ = 0;
  int64_t lastPingMs_ = 0;
};

}  // namespace heatboard

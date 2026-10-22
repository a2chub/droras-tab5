#include "heatboard/server_heartbeat.h"

#include <cmath>

namespace heatboard {
namespace {

bool elapsed(int64_t nowMs, int64_t sinceMs, int64_t durationMs) {
  // Timestamps are non-negative monotonic milliseconds. Subtraction avoids overflow
  // when uptime approaches the integer limit; a backwards clock never expires early.
  return nowMs >= sinceMs && nowMs - sinceMs >= durationMs;
}

}  // namespace

std::optional<ServerHeartbeatTiming> serverHeartbeatTiming(double intervalMs, double timeoutMs) {
  if (!std::isfinite(intervalMs) || !std::isfinite(timeoutMs) || intervalMs <= 0 || timeoutMs <= 0 ||
      intervalMs > kServerHeartbeatMaxWindowMs || timeoutMs > kServerHeartbeatMaxWindowMs ||
      std::floor(intervalMs) != intervalMs || std::floor(timeoutMs) != timeoutMs ||
      intervalMs + timeoutMs > kServerHeartbeatMaxWindowMs) {
    return std::nullopt;
  }
  return ServerHeartbeatTiming{static_cast<int64_t>(intervalMs), static_cast<int64_t>(timeoutMs)};
}

void ServerHeartbeat::reset() {
  phase_ = Phase::Idle;
  timing_ = {};
  phaseStartedMs_ = lastPingMs_ = 0;
}

void ServerHeartbeat::begin(int64_t nowMs) {
  reset();
  phase_ = Phase::AwaitingOpen;
  phaseStartedMs_ = nowMs;
}

bool ServerHeartbeat::engineOpened(ServerHeartbeatTiming timing, int64_t nowMs) {
  if (phase_ != Phase::AwaitingOpen || expired(nowMs) ||
      !serverHeartbeatTiming(static_cast<double>(timing.intervalMs), static_cast<double>(timing.timeoutMs))) {
    return false;
  }
  timing_ = timing;
  phase_ = Phase::AwaitingNamespace;
  phaseStartedMs_ = lastPingMs_ = nowMs;
  return true;
}

bool ServerHeartbeat::namespaceJoined(int64_t nowMs) {
  if (phase_ != Phase::AwaitingNamespace || expired(nowMs)) return false;
  phase_ = Phase::Healthy;
  return true;
}

bool ServerHeartbeat::receivedPing(int64_t nowMs) {
  if ((phase_ != Phase::AwaitingNamespace && phase_ != Phase::Healthy) || expired(nowMs)) return false;
  lastPingMs_ = nowMs;
  return true;
}

void ServerHeartbeat::fail() { phase_ = Phase::Failed; }

bool ServerHeartbeat::expired(int64_t nowMs) const {
  if (phase_ == Phase::AwaitingOpen) return elapsed(nowMs, phaseStartedMs_, kServerHandshakeTimeoutMs);
  if (phase_ == Phase::AwaitingNamespace && elapsed(nowMs, phaseStartedMs_, kServerHandshakeTimeoutMs)) return true;
  return (phase_ == Phase::AwaitingNamespace || phase_ == Phase::Healthy) &&
         elapsed(nowMs, lastPingMs_, timing_.intervalMs + timing_.timeoutMs);
}

bool ServerHeartbeat::poll(int64_t nowMs) {
  if (!expired(nowMs)) return false;
  fail();
  return true;
}

bool ServerHeartbeat::isHealthy(int64_t nowMs) const { return phase_ == Phase::Healthy && !expired(nowMs); }

}  // namespace heatboard

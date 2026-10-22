// Socket.IO client for the droras race server running on the Raspberry Pi.
//
// droras pushes the current heat to every connected client and accepts the operator
// commands (change heat, run the start sequence) over the same socket.
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "esp_event.h"
#include "esp_websocket_client.h"
#include "heatboard/server_address.h"
#include "heatboard/server_heartbeat.h"

class DrorasClient {
 public:
  static DrorasClient& instance();

  // Schedules this address (nullopt disables). Both methods run on the LAN task;
  // poll applies it once Wi-Fi is available and retries failed attempts.
  void configure(const std::optional<heatboard::ServerAddress>& address);
  void poll(bool wifiConnected);

  // Namespace joined and Engine.IO heartbeat still within its negotiated deadline.
  // This checks the deadline directly, so commands never depend on the next poll.
  bool isReady() const;

  // Operator commands. They return false when not connected or the send fails.
  //
  // setCurrentHeat also publishes the new heat to AppState straight away, without waiting
  // for the server to broadcast it back, so the display follows a touch without a network
  // round trip (the droras web UI does the same).
  bool setCurrentHeat(int heatNumber);
  bool startHeat();

 private:
  DrorasClient() = default;

  static void onEvent(void* arg, esp_event_base_t base, int32_t eventId, void* eventData);
  void onFrame(esp_websocket_client_handle_t client, const std::string& frame);
  void markUnresponsive(esp_websocket_client_handle_t client);
  void failLocked();
  void publishStatusLocked();
  void stopClient();
  void startClient();
  bool sendCommand(const std::string& frame);

  std::mutex clientMutex_;  // guards client_ against configure() racing with a command
  esp_websocket_client_handle_t client_ = nullptr;

  // Never hold stateMutex_ while calling WebSocket send/stop/destroy: send errors
  // can invoke callbacks synchronously, and stop waits for those callbacks to finish.
  mutable std::mutex stateMutex_;
  esp_websocket_client_handle_t eventClient_ = nullptr;
  heatboard::ServerHeartbeat heartbeat_;
  std::string addressText_;
  std::string publishedAddress_;
  int publishedStatus_ = -1;
  bool wifiAvailable_ = false;
  bool failureSeen_ = false;  // Keep failed status visible while retries are running.
  bool restartRequested_ = false;
  std::atomic<bool> ready_{false};

  // LAN-task-only scheduling state.
  std::optional<heatboard::ServerAddress> configuredAddress_;
  bool configurationChanged_ = false;
  int64_t retryAtMs_ = 0;
  // Broadcasts still expected for heat changes this board sent, and when the last was sent.
  std::atomic<int> pendingHeatEchoes_{0};
  std::atomic<int64_t> lastHeatCommandMs_{0};
  std::string frameBuffer_;  // protected by stateMutex_, including configuration resets
};

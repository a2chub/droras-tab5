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

class DrorasClient {
 public:
  static DrorasClient& instance();

  // Connects to `address` and keeps reconnecting; nullopt disconnects for good.
  // Not reentrant: call from one task only (the network task).
  void configure(const std::optional<heatboard::ServerAddress>& address);

  // True once the Socket.IO namespace is joined, i.e. commands will be delivered.
  bool isReady() const { return ready_; }

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
  void markDisconnected();
  bool sendCommand(const std::string& frame);

  std::mutex clientMutex_;  // guards client_ against configure() racing with a command
  esp_websocket_client_handle_t client_ = nullptr;
  std::string addressText_;
  std::atomic<bool> ready_{false};
  // Broadcasts still expected for heat changes this board sent, and when the last was sent.
  std::atomic<int> pendingHeatEchoes_{0};
  std::atomic<int64_t> lastHeatCommandMs_{0};
  std::string frameBuffer_;  // only touched from the websocket task
};

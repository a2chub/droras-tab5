// State shared between the network task (writer) and the UI loop (reader).
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "heatboard/heat_list.h"

enum class WifiStatus { NotConfigured, Failed, Connecting, Connected };
enum class HeatListStatus { Waiting, Loading, Ready, Failed };
enum class RpiStatus { Disabled, Connecting, Connected };
enum class HeatSource { None, Rpi, Firestore };

// Immutable once published, so the UI can keep drawing from it without holding the lock.
using HeatListPtr = std::shared_ptr<const std::vector<heatboard::Heat>>;

struct AppSnapshot {
  uint32_t revision = 0;

  WifiStatus wifi = WifiStatus::Connecting;
  int wifiSlot = -1;  // index of the Wi-Fi preset in use, -1 when none is configured
  std::string ipAddress;

  HeatListStatus heatListStatus = HeatListStatus::Waiting;
  std::string heatListError;
  HeatListPtr heats;

  RpiStatus rpi = RpiStatus::Disabled;
  std::string rpiAddress;  // "host:port", empty while no server is configured

  int currentHeat = 0;  // 0 until a source has reported one
  HeatSource heatSource = HeatSource::None;
};

class AppState {
 public:
  static AppState& instance();

  AppSnapshot snapshot() const;
  uint32_t revision() const;

  void setWifi(WifiStatus status, std::string ipAddress);
  void setWifiSlot(int slot);
  void setHeatListLoading();
  void setHeatList(HeatListPtr heats);
  void setHeatListFailed(std::string error);
  void setRpi(RpiStatus status, std::string address);
  void setCurrentHeat(int number, HeatSource source);
  // Keeps the last known heat on screen but marks that nothing is confirming it any more.
  void setHeatSourceLost();

  // Requests raised by the UI and consumed by the network task.
  void requestHeatListReload();
  bool takeHeatListReloadRequest();
  void requestServerReconfigure();
  bool takeServerReconfigureRequest();
  void requestWifiSlot(int slot);
  std::optional<int> takeWifiSlotRequest();

 private:
  AppState() = default;

  mutable std::mutex mutex_;
  AppSnapshot state_;
  bool heatListReloadRequested_ = false;
  bool serverReconfigureRequested_ = false;
  std::optional<int> requestedWifiSlot_;
};

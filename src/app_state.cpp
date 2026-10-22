#include "app_state.h"

#include <utility>

AppState& AppState::instance() {
  static AppState state;
  return state;
}

AppSnapshot AppState::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

uint32_t AppState::revision() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.revision;
}

void AppState::setWifi(WifiStatus status, std::string ipAddress) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.wifi = status;
  state_.ipAddress = std::move(ipAddress);
  if (status != WifiStatus::Connected) state_.wifiRssiDbm.reset();
  ++state_.revision;
}

void AppState::setWifiRssi(std::optional<int> rssiDbm) {
  std::lock_guard<std::mutex> lock(mutex_);
  // A radio query may finish after a disconnect event; never publish a stale sample.
  if (state_.wifi != WifiStatus::Connected ||
      (rssiDbm && (*rssiDbm >= 0 || *rssiDbm < -127))) {
    rssiDbm.reset();
  }
  if (state_.wifiRssiDbm == rssiDbm) return;
  state_.wifiRssiDbm = rssiDbm;
  ++state_.revision;
}

void AppState::setWifiSlot(int slot) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.wifiSlot = slot;
  ++state_.revision;
}

void AppState::setHeatListLoading() {
  std::lock_guard<std::mutex> lock(mutex_);
  // A reload keeps showing the list already on screen; only the first load has nothing to show.
  if (!state_.heats) {
    state_.heatListStatus = HeatListStatus::Loading;
    ++state_.revision;
  }
}

void AppState::setHeatList(HeatListPtr heats, HeatListSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.heats = std::move(heats);
  state_.heatListSource = source;
  state_.heatListStatus = HeatListStatus::Ready;
  state_.heatListError.clear();
  ++state_.revision;
}

bool AppState::hasHeatList() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.heats != nullptr;
}

void AppState::setHeatListFailed(std::string error) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.heatListError = std::move(error);
  // A failed reload must not blank a board that still has a usable list.
  if (!state_.heats) {
    state_.heatListStatus = HeatListStatus::Failed;
  }
  ++state_.revision;
}

void AppState::setRpi(RpiStatus status, std::string address) {
  std::lock_guard<std::mutex> lock(mutex_);
  state_.rpi = status;
  state_.rpiAddress = std::move(address);
  ++state_.revision;
}

void AppState::setCurrentHeat(int number, HeatSource source) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.currentHeat == number && state_.heatSource == source) {
    return;
  }
  state_.currentHeat = number;
  state_.heatSource = source;
  ++state_.revision;
}

void AppState::setHeatSourceLost() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_.heatSource == HeatSource::None) {
    return;
  }
  state_.heatSource = HeatSource::None;
  ++state_.revision;
}

void AppState::requestHeatListReload() {
  std::lock_guard<std::mutex> lock(mutex_);
  heatListReloadRequested_ = true;
}

bool AppState::takeHeatListReloadRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(heatListReloadRequested_, false);
}

void AppState::requestServerReconfigure() {
  std::lock_guard<std::mutex> lock(mutex_);
  serverReconfigureRequested_ = true;
}

bool AppState::takeServerReconfigureRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(serverReconfigureRequested_, false);
}

void AppState::requestWifiSlot(int slot) {
  std::lock_guard<std::mutex> lock(mutex_);
  requestedWifiSlot_ = slot;
}

std::optional<int> AppState::takeWifiSlotRequest() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(requestedWifiSlot_, std::nullopt);
}

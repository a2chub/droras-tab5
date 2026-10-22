// Settings screen with six tabs: Wi-Fi profile editing, Raspberry Pi server address
// (numeric keypad, plus pilot list reload), speaker volume / screen brightness, screen
// orientation (automatic from the accelerometer, or fixed by hand), hand gesture
// operation (on/off, hold time, which gesture does what), and a live camera view showing
// what the gesture recognition sees.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "app_state.h"
#include "heatboard/gesture_control.h"
#include "heatboard/orientation.h"
#include "heatboard/wifi_profile.h"
#include "ui_common.h"

class SettingsScreen {
 public:
  enum class Action { None, Close };

  // Draws the whole screen, reopening on the tab that was last in use.
  void show(const AppSnapshot& snapshot);

  // Call every loop iteration: keeps the connection details current.
  void update(const AppSnapshot& snapshot);

  Action handleTap(int x, int y, const AppSnapshot& snapshot);

 private:
  enum class Tab { Wifi, Server, Device, Orientation, Gesture, Camera };

  // Compare the details actually shown, independently of unrelated AppState changes.
  struct WifiInfoKey {
    WifiStatus status = WifiStatus::NotConfigured;
    std::string ssid;
    std::string address;
    std::optional<int> rssiDbm;
    bool operator==(const WifiInfoKey& other) const {
      return status == other.status && ssid == other.ssid && address == other.address && rssiDbm == other.rssiDbm;
    }
  };
  struct ServerInfoKey {
    RpiStatus status = RpiStatus::Disabled;
    bool waitingForWifi = false;
    std::string address;
    bool listAvailable = false;
    unsigned heatCount = 0;
    bool listFromRpi = false;
    std::string listError;
    bool operator==(const ServerInfoKey& other) const {
      return status == other.status && waitingForWifi == other.waitingForWifi && address == other.address &&
             listAvailable == other.listAvailable && heatCount == other.heatCount && listFromRpi == other.listFromRpi &&
             listError == other.listError;
    }
  };
  static WifiInfoKey wifiInfoKeyOf(const AppSnapshot& snapshot);
  static ServerInfoKey serverInfoKeyOf(const AppSnapshot& snapshot);

  void selectTab(Tab tab, const AppSnapshot& snapshot);
  void drawTabBar();
  void drawTabContent(const AppSnapshot& snapshot);
  void showMessage(const char* text, bool isError);
  void drawMessage();
  void drawConnectionHeader(const AppSnapshot& snapshot);

  void drawWifiTab(const AppSnapshot& snapshot);
  void drawWifiButtons(int activeSlot);
  void drawWifiInfo(const AppSnapshot& snapshot);
  void handleWifiTap(int x, int y, const AppSnapshot& snapshot);
  void beginWifiEdit(int slot);
  void discardWifiEdit();
  void drawWifiEditor();
  void drawWifiFields();
  void drawWifiKeyboard();
  void handleWifiEditorTap(int x, int y, const AppSnapshot& snapshot);
  void saveWifiProfile(const AppSnapshot& snapshot);

  void drawServerTab(const AppSnapshot& snapshot);
  void drawAddressField();
  void drawServerInfo(const AppSnapshot& snapshot);
  void handleServerTap(int x, int y);
  void saveServerAddress();

  void drawDeviceTab();
  void drawLevels();
  void handleDeviceTap(int x, int y);

  void drawOrientationTab();
  void drawOrientationButtons();
  void drawOrientationInfo();
  void handleOrientationTap(int x, int y);

  void drawGestureTab();
  void drawGestureControls();
  void drawGestureInfo();
  void handleGestureTap(int x, int y);
  void saveGestureSettings(const heatboard::GestureSettings& settings);

  void drawCameraTab();
  void drawCameraPreview();
  void drawCameraInfo();

  Tab tab_ = Tab::Wifi;
  std::string addressText_;
  std::string message_;
  bool messageIsError_ = false;
  uint32_t drawnRevision_ = 0;
  ui::ConnectionIndicatorState drawnConnectionIndicators_;
  WifiInfoKey drawnWifiInfo_;
  ServerInfoKey drawnServerInfo_;
  int drawnWifiSlot_ = -1;
  int editingWifiSlot_ = -1;
  heatboard::WifiProfile wifiDraft_;
  bool editingWifiPassword_ = false;
  bool wifiPasswordVisible_ = false;
  bool wifiKeyboardShift_ = false;
  heatboard::Orientation drawnSensed_ = heatboard::Orientation::Normal;
  bool drawnUpsideDown_ = false;
  heatboard::Gesture drawnSeen_ = heatboard::Gesture::None;
  int drawnSeenScore_ = -1;  // tenths
  uint32_t drawnPreviewFrame_ = 0;
  uint16_t* previewPixels_ = nullptr;  // a copy of the latest camera picture, allocated on first use
};

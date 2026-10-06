// Settings screen with four tabs: Wi-Fi preset selection, Raspberry Pi server address
// (numeric keypad, plus pilot list reload), speaker volume / screen brightness, and screen
// orientation (automatic from the accelerometer, or fixed by hand).
#pragma once

#include <cstdint>
#include <string>

#include "app_state.h"
#include "heatboard/orientation.h"

class SettingsScreen {
 public:
  enum class Action { None, Close };

  // Draws the whole screen, reopening on the tab that was last in use.
  void show(const AppSnapshot& snapshot);

  // Call every loop iteration: keeps the connection details current.
  void update(const AppSnapshot& snapshot);

  Action handleTap(int x, int y, const AppSnapshot& snapshot);

 private:
  enum class Tab { Wifi, Server, Device, Orientation };

  void selectTab(Tab tab, const AppSnapshot& snapshot);
  void drawTabBar();
  void drawTabContent(const AppSnapshot& snapshot);
  void showMessage(const char* text, bool isError);
  void drawMessage();

  void drawWifiTab(const AppSnapshot& snapshot);
  void drawWifiButtons(int activeSlot);
  void drawWifiInfo(const AppSnapshot& snapshot);
  void handleWifiTap(int x, int y, const AppSnapshot& snapshot);

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

  Tab tab_ = Tab::Wifi;
  std::string addressText_;
  std::string message_;
  bool messageIsError_ = false;
  uint32_t drawnRevision_ = 0;
  int drawnWifiSlot_ = -1;
  heatboard::Orientation drawnSensed_ = heatboard::Orientation::Normal;
  bool drawnUpsideDown_ = false;
};

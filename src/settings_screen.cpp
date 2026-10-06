#include "settings_screen.h"

#include <cstdio>

#include "heatboard/server_address.h"
#include "screen_orientation.h"
#include "settings_store.h"
#include "ui_common.h"
#include "wifi_station.h"

namespace {

using heatboard::Orientation;
using heatboard::OrientationMode;
using ui::Rect;

// --- Layout (1280x720) ---------------------------------------------------------------
constexpr Rect kBackButton = {1080, 12, 180, 64};
constexpr int kTitleBarBottom = 88;

constexpr int kTabCount = 4;
constexpr const char* kTabLabels[kTabCount] = {"Wi-Fi", "サーバー", "音量・明るさ", "画面の向き"};
constexpr int kMargin = 40;
constexpr int kTabTop = 100;
constexpr int kTabGap = 20;
constexpr int kTabWidth = (ui::kScreenWidth - 2 * kMargin - (kTabCount - 1) * kTabGap) / kTabCount;
constexpr int kTabHeight = 64;
constexpr int kContentTop = kTabTop + kTabHeight + 8;

// Wi-Fi tab: one large button per preset.
constexpr int kWifiButtonsTop = 224;
constexpr int kWifiButtonWidth = 590;
constexpr int kWifiButtonHeight = 110;
constexpr int kWifiButtonGap = 20;
constexpr int kWifiButtonColumns = 2;
constexpr Rect kWifiInfoArea = {kMargin, 484, 1200, 80};
constexpr Rect kWifiMessageArea = {kMargin, 580, 1200, 36};

// Server tab: editor and actions on the left, keypad on the right.
constexpr Rect kAddressField = {kMargin, 224, 580, 84};
constexpr Rect kServerMessageArea = {kMargin, 316, 580, 36};
constexpr Rect kSaveButton = {kMargin, 364, 280, 92};
constexpr Rect kClearButton = {340, 364, 280, 92};
constexpr Rect kReloadButton = {kMargin, 480, 580, 92};
constexpr Rect kServerInfoArea = {kMargin, 596, 600, 110};

constexpr int kKeypadLeft = 680;
constexpr int kKeypadTop = 184;
constexpr int kKeyWidth = 176;
constexpr int kKeyHeight = 92;
constexpr int kKeyGap = 14;
constexpr int kKeypadColumns = 3;
constexpr char kKeys[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '0', ':'};
constexpr int kKeyCount = sizeof(kKeys);
constexpr Rect kDeleteButton = {kKeypadLeft, kKeypadTop + 4 * (kKeyHeight + kKeyGap),
                                kKeypadColumns * kKeyWidth + (kKeypadColumns - 1) * kKeyGap, kKeyHeight};

// "255.255.255.255:65535"
constexpr std::size_t kMaxAddressLength = 21;

// Volume / brightness tab: [-] bar [+] per setting.
struct LevelControl {
  const char* title;
  int top;
  Rect minus() const { return {kMargin, top + 40, 150, 110}; }
  Rect bar() const { return {220, top + 60, 840, 70}; }
  Rect plus() const { return {1090, top + 40, 150, 110}; }
};
constexpr LevelControl kVolumeControl = {"タッチ音の音量", 196};
constexpr LevelControl kBrightnessControl = {"画面の明るさ", 400};
// Shared by the volume / brightness and orientation tabs.
constexpr Rect kBottomMessageArea = {kMargin, 600, 1200, 36};

// Orientation tab: one large button per mode, in OrientationMode order.
struct OrientationChoice {
  OrientationMode mode;
  const char* label;
};
constexpr OrientationChoice kOrientationChoices[] = {
    {OrientationMode::Auto, "自動（センサー）"},
    {OrientationMode::Normal, "通常の向き"},
    {OrientationMode::UpsideDown, "上下反転"},
};
constexpr int kOrientationChoiceCount = sizeof(kOrientationChoices) / sizeof(kOrientationChoices[0]);
constexpr int kOrientationButtonsTop = 236;
constexpr int kOrientationButtonGap = 20;
constexpr int kOrientationButtonWidth =
    (ui::kScreenWidth - 2 * kMargin - (kOrientationChoiceCount - 1) * kOrientationButtonGap) / kOrientationChoiceCount;
constexpr int kOrientationButtonHeight = 150;
constexpr Rect kOrientationInfoArea = {kMargin, 420, 1200, 120};

Rect tabRect(int index) { return {kMargin + index * (kTabWidth + kTabGap), kTabTop, kTabWidth, kTabHeight}; }

Rect wifiButtonRect(int slot) {
  return {kMargin + (slot % kWifiButtonColumns) * (kWifiButtonWidth + kWifiButtonGap),
          kWifiButtonsTop + (slot / kWifiButtonColumns) * (kWifiButtonHeight + kWifiButtonGap), kWifiButtonWidth,
          kWifiButtonHeight};
}

Rect orientationButtonRect(int index) {
  return {kMargin + index * (kOrientationButtonWidth + kOrientationButtonGap), kOrientationButtonsTop,
          kOrientationButtonWidth, kOrientationButtonHeight};
}

const char* orientationName(Orientation orientation) {
  return orientation == Orientation::UpsideDown ? "上下反転" : "通常の向き";
}

Rect keyRect(int index) {
  return {kKeypadLeft + (index % kKeypadColumns) * (kKeyWidth + kKeyGap),
          kKeypadTop + (index / kKeypadColumns) * (kKeyHeight + kKeyGap), kKeyWidth, kKeyHeight};
}

void drawLevelControl(const LevelControl& control, int level) {
  ui::drawButton(control.minus(), "－", ui::kColorGray, ui::kColorOnAccent, 40);
  ui::drawButton(control.plus(), "＋", ui::kColorGray, ui::kColorOnAccent, 40);

  const Rect bar = control.bar();
  ui::frame().fillRect(bar.x, bar.y, bar.w, bar.h, ui::kColorBackground);
  ui::frame().fillRoundRect(bar.x, bar.y, bar.w, bar.h, 10, ui::kColorPanel);
  if (level > 0) {
    ui::frame().fillRoundRect(bar.x, bar.y, bar.w * level / ui::kLevelMax, bar.h, 10, ui::kColorBlue);
  }
  char text[8];
  snprintf(text, sizeof(text), "%d", level);
  ui::drawFittedText(text, bar, 44, 44, ui::kColorText);
}

void presentContent() { ui::present(kContentTop, ui::kScreenHeight - kContentTop); }

// New level after a tap on a control, or -1 when the tap missed it. Tapping the bar jumps
// straight to that position.
int levelAfterTap(const LevelControl& control, int level, int x, int y) {
  if (control.minus().contains(x, y)) {
    return level - 1;
  }
  if (control.plus().contains(x, y)) {
    return level + 1;
  }
  const Rect bar = control.bar();
  if (bar.contains(x, y)) {
    return ((x - bar.x) * ui::kLevelMax + bar.w / 2) / bar.w;
  }
  return -1;
}

}  // namespace

void SettingsScreen::show(const AppSnapshot& snapshot) {
  ui::frame().fillScreen(ui::kColorBackground);
  ui::drawText("設定", kMargin, 22, 40, ui::kColorText);
  ui::drawButton(kBackButton, "戻る", ui::kColorGray, ui::kColorOnAccent, 32);
  ui::frame().drawFastHLine(0, kTitleBarBottom, ui::kScreenWidth, ui::kColorLine);
  ui::present(0, kTabTop);
  selectTab(tab_, snapshot);
}

void SettingsScreen::selectTab(Tab tab, const AppSnapshot& snapshot) {
  tab_ = tab;
  message_.clear();
  messageIsError_ = false;
  if (tab == Tab::Server) {
    const auto stored = settings_store::loadServerAddress();
    addressText_ = stored ? heatboard::formatServerAddress(*stored) : "";
  }

  drawTabBar();
  drawTabContent(snapshot);
  ui::present(kTabTop, ui::kScreenHeight - kTabTop);
  drawnRevision_ = snapshot.revision;
}

void SettingsScreen::drawTabBar() {
  ui::frame().fillRect(0, kTabTop, ui::kScreenWidth, kTabHeight, ui::kColorBackground);
  for (int i = 0; i < kTabCount; ++i) {
    const bool active = i == static_cast<int>(tab_);
    ui::drawButton(tabRect(i), kTabLabels[i], active ? ui::kColorBlue : ui::kColorPanel,
                   active ? ui::kColorOnAccent : ui::kColorText, 32);
  }
}

void SettingsScreen::drawTabContent(const AppSnapshot& snapshot) {
  ui::frame().fillRect(0, kContentTop, ui::kScreenWidth, ui::kScreenHeight - kContentTop, ui::kColorBackground);
  switch (tab_) {
    case Tab::Wifi: drawWifiTab(snapshot); break;
    case Tab::Server: drawServerTab(snapshot); break;
    case Tab::Device: drawDeviceTab(); break;
    case Tab::Orientation: drawOrientationTab(); break;
  }
}

void SettingsScreen::update(const AppSnapshot& snapshot) {
  // Follows the sensor live, so that turning the Tab5 shows what Auto would do.
  if (tab_ == Tab::Orientation &&
      (screen_orientation::sensed() != drawnSensed_ || ui::upsideDown() != drawnUpsideDown_)) {
    drawOrientationInfo();
    presentContent();
  }
  if (snapshot.revision == drawnRevision_) {
    return;
  }
  if (tab_ == Tab::Wifi) {
    if (snapshot.wifiSlot != drawnWifiSlot_) {
      drawWifiButtons(snapshot.wifiSlot);
    }
    drawWifiInfo(snapshot);
  } else if (tab_ == Tab::Server) {
    drawServerInfo(snapshot);
  }
  presentContent();
  drawnRevision_ = snapshot.revision;
}

SettingsScreen::Action SettingsScreen::handleTap(int x, int y, const AppSnapshot& snapshot) {
  if (kBackButton.contains(x, y)) {
    ui::beepAccepted();
    return Action::Close;
  }
  for (int i = 0; i < kTabCount; ++i) {
    if (tabRect(i).contains(x, y)) {
      ui::beepAccepted();
      if (i != static_cast<int>(tab_)) {
        selectTab(static_cast<Tab>(i), snapshot);
      }
      return Action::None;
    }
  }

  switch (tab_) {
    case Tab::Wifi: handleWifiTap(x, y, snapshot); break;
    case Tab::Server: handleServerTap(x, y); break;
    case Tab::Device: handleDeviceTap(x, y); break;
    case Tab::Orientation: handleOrientationTap(x, y); break;
  }
  // The handlers redraw only small pieces of the tab; one copy covers whichever they touched.
  presentContent();
  return Action::None;
}

void SettingsScreen::showMessage(const char* text, bool isError) {
  message_ = text;
  messageIsError_ = isError;
  drawMessage();
}

void SettingsScreen::drawMessage() {
  const Rect& area = tab_ == Tab::Wifi ? kWifiMessageArea : tab_ == Tab::Server ? kServerMessageArea : kBottomMessageArea;
  ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);
  ui::drawText(message_.c_str(), area.x, area.y + 4, 24, messageIsError_ ? ui::kColorRed : ui::kColorGreen);
}

// --- Wi-Fi tab -----------------------------------------------------------------------

void SettingsScreen::drawWifiTab(const AppSnapshot& snapshot) {
  ui::drawText("接続するWi-Fiをタップして選択（登録は src/app_secrets.h）", kMargin, kContentTop + 12, 24,
                  ui::kColorMutedText);
  drawWifiButtons(snapshot.wifiSlot);
  drawWifiInfo(snapshot);
}

void SettingsScreen::drawWifiButtons(int activeSlot) {
  constexpr int kLabelSize = 44;
  constexpr int kMinLabelSize = 20;
  for (int slot = 0; slot < wifi_station::kNetworkSlots; ++slot) {
    const Rect area = wifiButtonRect(slot);
    ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);
    if (wifi_station::ssid(slot)[0] == '\0') {
      ui::drawOutlinedRoundRect(area, 14, ui::kColorDisabled, ui::kColorBackground);
      ui::drawFittedText("未登録", area, kLabelSize, kMinLabelSize, ui::kColorDisabled);
      continue;
    }
    const bool active = slot == activeSlot;
    ui::frame().fillRoundRect(area.x, area.y, area.w, area.h, 14, active ? ui::kColorBlue : ui::kColorPanel);
    ui::drawFittedText(wifi_station::label(slot), area, kLabelSize, kMinLabelSize,
                       active ? ui::kColorOnAccent : ui::kColorText);
  }
  drawnWifiSlot_ = activeSlot;
}

void SettingsScreen::drawWifiInfo(const AppSnapshot& snapshot) {
  const Rect& area = kWifiInfoArea;
  ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);

  char line[128];
  const char* ssid = wifi_station::ssid(snapshot.wifiSlot);
  switch (snapshot.wifi) {
    case WifiStatus::NotConfigured: snprintf(line, sizeof(line), "状態: Wi-Fiが1件も登録されていません"); break;
    case WifiStatus::Failed: snprintf(line, sizeof(line), "状態: 無線モジュールの初期化に失敗"); break;
    case WifiStatus::Connecting: snprintf(line, sizeof(line), "状態: %s に接続中…", ssid); break;
    case WifiStatus::Connected:
      snprintf(line, sizeof(line), "状態: %s に接続済み／このTab5のIPアドレス %s", ssid, snapshot.ipAddress.c_str());
      break;
  }
  ui::drawText(line, area.x, area.y, 28, ui::kColorText);
  ui::drawText("Tab5が対応するのは2.4GHz帯のWi-Fiのみです", area.x, area.y + 44, 20, ui::kColorMutedText);
}

void SettingsScreen::handleWifiTap(int x, int y, const AppSnapshot& snapshot) {
  for (int slot = 0; slot < wifi_station::kNetworkSlots; ++slot) {
    if (!wifiButtonRect(slot).contains(x, y)) {
      continue;
    }
    if (wifi_station::ssid(slot)[0] == '\0') {
      ui::beepRejected();
      showMessage("このボタンにはWi-Fiが登録されていません", true);
    } else if (slot == snapshot.wifiSlot) {
      ui::beepAccepted();
      showMessage("このWi-Fiを使用中です", false);
    } else {
      // The radio belongs to the network task; the buttons follow once it has switched.
      AppState::instance().requestWifiSlot(slot);
      ui::beepAccepted();
      showMessage("Wi-Fiを切り替えます", false);
    }
    return;
  }
}

// --- Server tab ----------------------------------------------------------------------

void SettingsScreen::drawServerTab(const AppSnapshot& snapshot) {
  ui::drawText("RPiサーバーのIPアドレス（空欄で保存するとRPiを使いません）", kMargin, kContentTop + 12, 20,
                  ui::kColorMutedText);
  drawAddressField();
  ui::drawButton(kSaveButton, "保存", ui::kColorGreen, ui::kColorOnAccent, 36);
  ui::drawButton(kClearButton, "クリア", ui::kColorGray, ui::kColorOnAccent, 36);
  ui::drawButton(kReloadButton, "選手リストを再取得", ui::kColorBlue, ui::kColorOnAccent, 36);

  for (int i = 0; i < kKeyCount; ++i) {
    const char label[2] = {kKeys[i], '\0'};
    ui::drawButton(keyRect(i), label, ui::kColorPanel, ui::kColorText, 40);
  }
  ui::drawButton(kDeleteButton, "1文字削除", ui::kColorPanel, ui::kColorText, 36);
  drawServerInfo(snapshot);
}

void SettingsScreen::drawAddressField() {
  const Rect& field = kAddressField;
  ui::drawOutlinedRoundRect(field, 10, ui::kColorGray, ui::kColorPanel);
  const std::string shown = addressText_ + "_";
  ui::drawFittedText(shown.c_str(), field, 44, 28, ui::kColorText);
}

void SettingsScreen::drawServerInfo(const AppSnapshot& snapshot) {
  const Rect& area = kServerInfoArea;
  ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);

  char line[128];
  switch (snapshot.rpi) {
    case RpiStatus::Disabled: snprintf(line, sizeof(line), "RPi: 未設定（Firestoreのみ使用）"); break;
    case RpiStatus::Connecting: snprintf(line, sizeof(line), "RPi: %s に未接続", snapshot.rpiAddress.c_str()); break;
    case RpiStatus::Connected: snprintf(line, sizeof(line), "RPi: %s に接続中", snapshot.rpiAddress.c_str()); break;
  }
  ui::drawText(line, area.x, area.y, 24, ui::kColorText);

  const unsigned heatCount = snapshot.heats ? static_cast<unsigned>(snapshot.heats->size()) : 0;
  const int y = area.y + 38;
  if (!snapshot.heatListError.empty()) {
    // Shown even while an older list is still in use, so a failed reload is not silent.
    snprintf(line, sizeof(line), "選手リスト: %u ヒート", heatCount);
    ui::drawText(line, area.x, y, 24, ui::kColorText);
    // The reason can be long; keep it out of the keypad next to this column.
    snprintf(line, sizeof(line), "取得エラー: %s", snapshot.heatListError.c_str());
    ui::drawText(line, area.x, y + 36, 20, ui::kColorRed, &area);
  } else if (snapshot.heats) {
    snprintf(line, sizeof(line), "選手リスト: %u ヒート", heatCount);
    ui::drawText(line, area.x, y, 24, ui::kColorText);
  } else {
    ui::drawText("選手リスト: 未取得", area.x, y, 24, ui::kColorText);
  }
}

void SettingsScreen::handleServerTap(int x, int y) {
  for (int i = 0; i < kKeyCount; ++i) {
    if (!keyRect(i).contains(x, y)) {
      continue;
    }
    if (addressText_.size() >= kMaxAddressLength) {
      ui::beepRejected();
      return;
    }
    addressText_.push_back(kKeys[i]);
    ui::beepAccepted();
    drawAddressField();
    return;
  }

  if (kDeleteButton.contains(x, y) || kClearButton.contains(x, y)) {
    if (addressText_.empty()) {
      ui::beepRejected();
      return;
    }
    if (kClearButton.contains(x, y)) {
      addressText_.clear();
    } else {
      addressText_.pop_back();
    }
    ui::beepAccepted();
    drawAddressField();
  } else if (kSaveButton.contains(x, y)) {
    saveServerAddress();
  } else if (kReloadButton.contains(x, y)) {
    AppState::instance().requestHeatListReload();
    ui::beepAccepted();
    showMessage("選手リストの再取得を開始しました", false);
  }
}

void SettingsScreen::saveServerAddress() {
  std::optional<heatboard::ServerAddress> address;
  if (!addressText_.empty()) {
    address = heatboard::parseServerAddress(addressText_);
    if (!address) {
      ui::beepRejected();
      showMessage("形式が正しくありません（例 192.168.1.20:8000）", true);
      return;
    }
  }

  if (settings_store::saveServerAddress(address) != ESP_OK) {
    ui::beepRejected();
    showMessage("保存に失敗しました", true);
    return;
  }

  // Show what was actually stored (default port filled in, leading zeros removed).
  addressText_ = address ? heatboard::formatServerAddress(*address) : "";
  AppState::instance().requestServerReconfigure();
  ui::beepAccepted();
  drawAddressField();
  showMessage(address ? "保存しました。RPiに接続します" : "保存しました。RPiは使いません", false);
}

// --- Volume / brightness tab -----------------------------------------------------------

void SettingsScreen::drawDeviceTab() {
  ui::drawText(kVolumeControl.title, kMargin, kVolumeControl.top, 28, ui::kColorText);
  ui::drawText(kBrightnessControl.title, kMargin, kBrightnessControl.top, 28, ui::kColorText);
  drawLevels();
}

void SettingsScreen::drawLevels() {
  drawLevelControl(kVolumeControl, ui::volumeLevel());
  drawLevelControl(kBrightnessControl, ui::brightnessLevel());
}

void SettingsScreen::handleDeviceTap(int x, int y) {
  const int volume = levelAfterTap(kVolumeControl, ui::volumeLevel(), x, y);
  const int brightness = levelAfterTap(kBrightnessControl, ui::brightnessLevel(), x, y);
  if (volume < 0 && brightness < 0) {
    return;
  }

  bool saved = true;
  if (volume >= 0) {
    ui::setVolumeLevel(volume);
    saved = settings_store::saveVolumeLevel(ui::volumeLevel()) == ESP_OK;
  } else {
    ui::setBrightnessLevel(brightness);
    saved = settings_store::saveBrightnessLevel(ui::brightnessLevel()) == ESP_OK;
  }
  // Played after the change so the click itself demonstrates the new volume.
  ui::beepAccepted();
  drawLevels();
  if (!saved) {
    showMessage("設定を保存できませんでした（電源を切ると元に戻ります）", true);
  }
}

// --- Orientation tab -------------------------------------------------------------------

void SettingsScreen::drawOrientationTab() {
  ui::drawText("自動にすると、Tab5を逆さに持ったときに表示も上下反転します", kMargin, kContentTop + 12, 24,
               ui::kColorMutedText);
  drawOrientationButtons();
  drawOrientationInfo();
}

void SettingsScreen::drawOrientationButtons() {
  for (int i = 0; i < kOrientationChoiceCount; ++i) {
    const Rect area = orientationButtonRect(i);
    const bool active = kOrientationChoices[i].mode == screen_orientation::mode();
    // Cleared first: the corners would otherwise keep a fringe of the previous colour.
    ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);
    ui::drawButton(area, kOrientationChoices[i].label, active ? ui::kColorBlue : ui::kColorPanel,
                   active ? ui::kColorOnAccent : ui::kColorText, 40);
  }
}

void SettingsScreen::drawOrientationInfo() {
  const Rect& area = kOrientationInfoArea;
  ui::frame().fillRect(area.x, area.y, area.w, area.h, ui::kColorBackground);

  const bool upsideDown = ui::upsideDown();
  const Orientation sensed = screen_orientation::sensed();
  char line[128];
  snprintf(line, sizeof(line), "現在の表示: %s", orientationName(upsideDown ? Orientation::UpsideDown : Orientation::Normal));
  ui::drawText(line, area.x, area.y, 28, ui::kColorText);
  if (screen_orientation::sensorAvailable()) {
    snprintf(line, sizeof(line), "加速度センサーの判定: %s（平らに置いている間は直前の判定のまま）",
             orientationName(sensed));
    ui::drawText(line, area.x, area.y + 48, 24, ui::kColorMutedText);
  } else {
    ui::drawText("加速度センサーが見つかりません（自動では通常の向きで表示します）", area.x, area.y + 48, 24,
                 ui::kColorRed);
  }
  drawnSensed_ = sensed;
  drawnUpsideDown_ = upsideDown;
}

void SettingsScreen::handleOrientationTap(int x, int y) {
  for (int i = 0; i < kOrientationChoiceCount; ++i) {
    if (!orientationButtonRect(i).contains(x, y)) {
      continue;
    }
    const OrientationMode mode = kOrientationChoices[i].mode;
    ui::beepAccepted();
    if (mode == screen_orientation::mode()) {
      return;
    }
    const bool saved = settings_store::saveOrientationMode(mode) == ESP_OK;
    screen_orientation::setMode(mode);
    drawOrientationButtons();
    drawOrientationInfo();
    if (!saved) {
      showMessage("設定を保存できませんでした（電源を切ると元に戻ります）", true);
    }
    return;
  }
}

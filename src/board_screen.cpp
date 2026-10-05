#include "board_screen.h"

#include <cstdio>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "droras_client.h"
#include "heatboard/heat_window.h"
#include "ui_common.h"

namespace {

using ui::Rect;

const char* TAG = "board";

// --- Layout (1280x720) ---------------------------------------------------------------
constexpr Rect kStatusBar = {0, 0, ui::kScreenWidth, 56};
constexpr Rect kSettingsButton = {1116, 6, 152, 44};
// Larger than the drawn button: the bar is thin and the button sits in a screen corner.
constexpr Rect kSettingsTouchArea = {1080, 0, 200, 72};

constexpr Rect kTableHeader = {0, 60, ui::kScreenWidth, 44};
constexpr int kOtherRowHeight = 84;
constexpr int kCurrentRowHeight = 132;
constexpr int kTableBodyTop = kTableHeader.y + kTableHeader.h;
constexpr int kTableBodyHeight = 3 * kOtherRowHeight + kCurrentRowHeight;

// The two left columns are kept narrow: every pixel given to them makes the names smaller.
constexpr int kHeatColumnWidth = 76;
constexpr int kClassColumnWidth = 144;
constexpr int kPilotColumnLeft = kHeatColumnWidth + kClassColumnWidth;
constexpr int kPilotColumns = 4;
constexpr int kPilotColumnWidth = (ui::kScreenWidth - kPilotColumnLeft) / kPilotColumns;

constexpr Rect kProgressBar = {40, 504, 1200, 44};
// Left to right: previous, next, start/stop.
constexpr Rect kPreviousButton = {40, 568, 380, 136};
constexpr Rect kNextButton = {450, 568, 380, 136};
constexpr Rect kStartButton = {860, 568, 380, 136};
constexpr int kButtonTextSize = 46;

// Video channel of each pilot column, as labelled on the droras operator UI.
constexpr const char* kChannelLabels[kPilotColumns] = {"R2 / 5695", "F1 / 5740", "R4 / 5769", "R5 / 5806"};

// --- Type sizes (pixels) ---------------------------------------------------------------
// Names in the current heat are as large as a six-character Japanese name allows in its
// column; the other rows are clearly smaller. Longer names shrink to fit, down to the minimum.
struct RowStyle {
  int height;
  int nameSize;
  int minNameSize;
  int heatSize;
  int classSize;
};
constexpr RowStyle kCurrentRowStyle = {kCurrentRowHeight, 48, 24, 48, 26};
constexpr RowStyle kOtherRowStyle = {kOtherRowHeight, 36, 20, 34, 20};
constexpr int kMinClassSize = 14;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

void formatMinSec(char* out, std::size_t size, int seconds) {
  snprintf(out, size, "%d:%02d", seconds / 60, seconds % 60);
}

// Class names such as "準々決勝 Expert" are too wide for the narrow class column on one
// line, so they are stacked at the first space.
void drawClassName(const std::string& className, const Rect& cell, int size) {
  const std::size_t space = className.find(' ');
  if (space == std::string::npos) {
    ui::drawFittedText(className.c_str(), cell, size, kMinClassSize, ui::kColorText);
    return;
  }
  const std::string upper = className.substr(0, space);
  const std::string lower = className.substr(space + 1);
  const int lineHeight = size + 6;
  const int top = cell.centerY() - lineHeight;
  ui::drawFittedText(upper.c_str(), {cell.x, top, cell.w, lineHeight}, size, kMinClassSize, ui::kColorText);
  ui::drawFittedText(lower.c_str(), {cell.x, top + lineHeight, cell.w, lineHeight}, size, kMinClassSize,
                     ui::kColorText);
}

void drawHeatRow(const heatboard::Heat& heat, int top, bool current) {
  const RowStyle& style = current ? kCurrentRowStyle : kOtherRowStyle;
  const uint32_t background = current ? ui::kColorCurrentRow : ui::kColorBackground;
  ui::frame().fillRect(0, top, ui::kScreenWidth, style.height, background);
  ui::frame().drawFastHLine(0, top + style.height - 1, ui::kScreenWidth, ui::kColorLine);

  char number[12];
  snprintf(number, sizeof(number), "%d", heat.number);
  ui::drawFittedText(number, {0, top, kHeatColumnWidth, style.height}, style.heatSize, 20, ui::kColorText);
  drawClassName(heat.className, {kHeatColumnWidth, top, kClassColumnWidth, style.height}, style.classSize);

  for (int column = 0; column < kPilotColumns; ++column) {
    const Rect cell = {kPilotColumnLeft + column * kPilotColumnWidth, top, kPilotColumnWidth, style.height};
    const bool vacant = column >= static_cast<int>(heat.pilots.size()) || heat.pilots[column].empty();
    const char* name = vacant ? "-" : heat.pilots[column].c_str();
    ui::drawFittedText(name, cell, style.nameSize, style.minNameSize, vacant ? ui::kColorMutedText : ui::kColorText);
  }
}

constexpr int kTableRows = 4;

// Draws one of the four rows of the table whose current heat is `window.current`.
void drawTableRow(const std::vector<heatboard::Heat>& heats, const heatboard::HeatWindow& window, int row) {
  switch (row) {
    case 0: drawHeatRow(heats[window.previous], kTableBodyTop, false); break;
    case 1: drawHeatRow(heats[window.current], kTableBodyTop + kOtherRowHeight, true); break;
    case 2: drawHeatRow(heats[window.next], kTableBodyTop + kOtherRowHeight + kCurrentRowHeight, false); break;
    case 3:
      drawHeatRow(heats[window.afterNext], kTableBodyTop + 2 * kOtherRowHeight + kCurrentRowHeight, false);
      break;
    default: break;
  }
}

void drawTableMessage(const char* line1, const char* line2) {
  const Rect body = {0, kTableBodyTop, ui::kScreenWidth, kTableBodyHeight};
  ui::frame().fillRect(body.x, body.y, body.w, body.h, ui::kColorBackground);
  ui::drawFittedText(line1, {0, body.centerY() - 60, body.w, 60}, 40, 24, ui::kColorMutedText);
  if (line2 != nullptr && line2[0] != '\0') {
    ui::drawFittedText(line2, {0, body.centerY() + 10, body.w, 40}, 26, 16, ui::kColorMutedText);
  }
}

}  // namespace

bool BoardScreen::canCommand(const AppSnapshot& snapshot) const {
  return snapshot.rpi == RpiStatus::Connected && snapshot.heats &&
         heatboard::computeHeatWindow(*snapshot.heats, snapshot.currentHeat).has_value();
}

void BoardScreen::stopTimer() { timerRunning_ = false; }

void BoardScreen::toggleStart(const AppSnapshot& snapshot) {
  // Like the web UI, pressing the button again only stops this board's timer; the start
  // sequence already running on the Raspberry Pi cannot be cancelled.
  if (timerRunning_) {
    stopTimer();
    ui::beepAccepted();
    return;
  }
  if (!canCommand(snapshot) || !DrorasClient::instance().startHeat()) {
    ui::beepRejected();
    return;
  }
  timerRunning_ = true;
  timerStartMs_ = nowMs();
  ui::beepAccepted();
}

bool BoardScreen::changeHeat(const AppSnapshot& snapshot, bool forward) {
  if (!canCommand(snapshot)) {
    return false;
  }
  const auto target = forward ? heatboard::nextHeatNumber(*snapshot.heats, snapshot.currentHeat)
                              : heatboard::previousHeatNumber(*snapshot.heats, snapshot.currentHeat);
  // setCurrentHeat publishes the new heat immediately, so the table follows on the next
  // loop iteration without waiting for the server.
  if (!target || !DrorasClient::instance().setCurrentHeat(*target)) {
    return false;
  }
  stopTimer();
  return true;
}

BoardScreen::Action BoardScreen::handleTap(int x, int y, const AppSnapshot& snapshot) {
  if (kSettingsTouchArea.contains(x, y)) {
    ui::beepAccepted();
    return Action::OpenSettings;
  }
  if (kStartButton.contains(x, y)) {
    toggleStart(snapshot);
  } else if (kPreviousButton.contains(x, y) || kNextButton.contains(x, y)) {
    if (changeHeat(snapshot, kNextButton.contains(x, y))) {
      ui::beepAccepted();
    } else {
      ui::beepRejected();
    }
  }
  return Action::None;
}

void BoardScreen::handleKey(char key, const AppSnapshot& snapshot) {
  switch (key) {
    case '1': toggleStart(snapshot); break;
    case '2': changeHeat(snapshot, false); break;
    case '3': changeHeat(snapshot, true); break;
    default: break;
  }
}

BoardScreen::TableKey BoardScreen::tableKeyOf(const AppSnapshot& snapshot) {
  return {snapshot.heats.get(), snapshot.currentHeat, snapshot.wifi, snapshot.heatListStatus, snapshot.heatListError};
}

// Status line shown for `snapshot`, prefixed with the indicator colour so that the string
// alone tells whether the bar needs repainting.
std::string BoardScreen::statusOf(const AppSnapshot& snapshot, uint32_t& dot) {
  dot = ui::kColorRed;
  if (snapshot.wifi == WifiStatus::NotConfigured) {
    return "Wi-Fi未設定";
  }
  if (snapshot.wifi == WifiStatus::Failed) {
    return "無線モジュールの初期化に失敗";
  }
  if (snapshot.wifi == WifiStatus::Connecting) {
    return "Wi-Fiに接続中…";
  }
  if (snapshot.rpi == RpiStatus::Connected) {
    dot = ui::kColorGreen;
    return "RPiに接続中  " + snapshot.rpiAddress;
  }
  const std::string rpi = snapshot.rpi == RpiStatus::Disabled ? "RPi未設定" : "RPi " + snapshot.rpiAddress + " に未接続";
  if (snapshot.heatSource == HeatSource::Firestore) {
    dot = ui::kColorAmber;
    return "Firestoreから取得中（" + rpi + "）";
  }
  return "ヒート情報を取得できません（" + rpi + "）";
}

void BoardScreen::show(const AppSnapshot& snapshot) {
  if (!preparedSetUp_) {
    setUpPreparedTables();
  }
  ui::frame().fillScreen(ui::kColorBackground);
  drawTableHeader();
  drawStatusBar(snapshot);
  drawTable(snapshot);
  drawProgress();
  drawButtons(snapshot);
  ui::presentAll();
  drawnRevision_ = snapshot.revision;
}

void BoardScreen::update(const AppSnapshot& snapshot) {
  if (timerRunning_) {
    const int elapsed = static_cast<int>((nowMs() - timerStartMs_) / 1000);
    if (elapsed > app_config::kRaceTimerSeconds) {
      stopTimer();
      // Best effort: if the Raspberry Pi went away meanwhile the heat simply stays.
      changeHeat(snapshot, true);
    }
  }

  // The snapshot revision moves for every state change, most of which leave a given region
  // as it is; each region is repainted only when what it shows is different.
  bool tableChanged = false;
  if (snapshot.revision != drawnRevision_) {
    drawnRevision_ = snapshot.revision;
    uint32_t dot = 0;
    if (statusOf(snapshot, dot) != drawnStatus_ || dot != drawnStatusDot_) {
      drawStatusBar(snapshot);
      ui::present(kStatusBar.y, kStatusBar.h);
    }
    if (!(tableKeyOf(snapshot) == drawnTable_)) {
      const int64_t startedMs = nowMs();
      PreparedTable* table = preparedFor(snapshot, snapshot.currentHeat);
      if (table != nullptr) {
        // Usually complete already; otherwise (a jump, or a tap before the preparation
        // finished) the missing rows are drawn now.
        while (table->rowsDrawn < kTableRows) {
          drawNextRow(*table);
        }
        drawnTable_ = tableKeyOf(snapshot);
      } else {
        drawTable(snapshot);  // a message, or no memory for prepared tables
      }
      const int64_t drawnMs = nowMs();
      if (table != nullptr) {
        table->canvas.present(kTableBodyTop, kTableBodyHeight);
      } else {
        ui::present(kTableBodyTop, kTableBodyHeight);
      }
      // Logged per change so that a slow switch shows up in the serial log: "drawn in" is
      // 0 ms when the table was ready in advance.
      ESP_LOGI(TAG, "heat %d: drawn in %d ms, copied to the panel in %d ms", snapshot.currentHeat,
               static_cast<int>(drawnMs - startedMs), static_cast<int>(nowMs() - drawnMs));
      tableChanged = true;
    }
  }
  if (!tableChanged) {
    prepareNeighbours(snapshot);
  }
  const bool commandable = canCommand(snapshot);
  if (timerRunning_ != drawnTimerRunning_ || commandable != drawnCanCommand_) {
    drawButtons(snapshot);
    drawnProgressSecond_ = -1;  // the bar resets together with the start button
    ui::present(kStartButton.y, kStartButton.h);
  }
  const int second = timerRunning_ ? static_cast<int>((nowMs() - timerStartMs_) / 1000) : 0;
  if (second != drawnProgressSecond_) {
    drawProgress();
    ui::present(kProgressBar.y, kProgressBar.h);
  }
}

void BoardScreen::drawStatusBar(const AppSnapshot& snapshot) {
  uint32_t dot = 0;
  const std::string text = statusOf(snapshot, dot);
  ui::frame().fillRect(kStatusBar.x, kStatusBar.y, kStatusBar.w, kStatusBar.h, ui::kColorPanel);
  ui::frame().fillCircle(28, kStatusBar.centerY(), 10, dot);
  ui::drawText(text.c_str(), 52, kStatusBar.centerY() - 13, 26, ui::kColorText);
  ui::drawButton(kSettingsButton, "設定", ui::kColorGray, ui::kColorOnAccent, 26);
  drawnStatus_ = text;
  drawnStatusDot_ = dot;
}

void BoardScreen::drawTableHeader() {
  ui::frame().fillRect(kTableHeader.x, kTableHeader.y, kTableHeader.w, kTableHeader.h, ui::kColorBackground);
  ui::frame().drawFastHLine(0, kTableBodyTop - 2, ui::kScreenWidth, ui::kColorLine);
  ui::frame().drawFastHLine(0, kTableBodyTop - 1, ui::kScreenWidth, ui::kColorLine);
  constexpr int kHeaderSize = 26;
  constexpr int kMinHeaderSize = 16;
  ui::drawFittedText("Heat", {0, kTableHeader.y, kHeatColumnWidth, kTableHeader.h}, kHeaderSize, kMinHeaderSize,
                     ui::kColorText);
  ui::drawFittedText("Class", {kHeatColumnWidth, kTableHeader.y, kClassColumnWidth, kTableHeader.h}, kHeaderSize,
                     kMinHeaderSize, ui::kColorText);
  for (int column = 0; column < kPilotColumns; ++column) {
    ui::drawFittedText(kChannelLabels[column],
                       {kPilotColumnLeft + column * kPilotColumnWidth, kTableHeader.y, kPilotColumnWidth, kTableHeader.h},
                       kHeaderSize, kMinHeaderSize, ui::kColorText);
  }
}

void BoardScreen::drawTable(const AppSnapshot& snapshot) {
  drawnTable_ = tableKeyOf(snapshot);

  if (!snapshot.heats) {
    switch (snapshot.wifi) {
      case WifiStatus::NotConfigured:
        drawTableMessage("Wi-Fiが未設定です", "src/app_secrets.h にSSIDとパスワードを記入して、書き込み直してください");
        return;
      case WifiStatus::Failed:
        drawTableMessage("無線モジュールを初期化できませんでした", "電源を入れ直してください");
        return;
      case WifiStatus::Connecting:
        drawTableMessage("Wi-Fiに接続しています…", nullptr);
        return;
      case WifiStatus::Connected:
        break;
    }
    if (snapshot.heatListStatus == HeatListStatus::Failed) {
      const std::string detail = snapshot.heatListError + "／自動で再試行します";
      drawTableMessage("選手リストを取得できません", detail.c_str());
    } else {
      drawTableMessage("選手リストを取得しています…", nullptr);
    }
    return;
  }

  const auto window = heatboard::computeHeatWindow(*snapshot.heats, snapshot.currentHeat);
  if (!window) {
    if (snapshot.currentHeat == 0) {
      drawTableMessage("現在のヒート情報を待っています…", nullptr);
    } else {
      char message[64];
      snprintf(message, sizeof(message), "ヒート %d は選手リストにありません", snapshot.currentHeat);
      drawTableMessage(message, "設定画面から選手リストを再取得してください");
    }
    return;
  }

  for (int row = 0; row < kTableRows; ++row) {
    drawTableRow(*snapshot.heats, *window, row);
  }
}

// --- Tables prepared in advance ----------------------------------------------------------

void BoardScreen::setUpPreparedTables() {
  preparedSetUp_ = true;
  preparedAvailable_ = true;
  for (PreparedTable& table : prepared_) {
    if (!table.canvas.beginLike(ui::frame())) {
      // Not fatal: every heat change then draws its table on the spot, as before.
      ESP_LOGW(TAG, "no memory to prepare tables in advance; heat changes will be slower");
      preparedAvailable_ = false;
      return;
    }
  }
}

BoardScreen::PreparedTable* BoardScreen::findPrepared(const HeatListPtr& heats, int heatNumber) {
  for (PreparedTable& table : prepared_) {
    if (table.heats == heats && table.heatNumber == heatNumber) {
      return &table;
    }
  }
  return nullptr;
}

// The slot holding (or now assigned to) the table of `heatNumber`; nullptr when that heat has
// no table to draw or the slots are unavailable. A newly assigned slot starts with no rows.
BoardScreen::PreparedTable* BoardScreen::preparedFor(const AppSnapshot& snapshot, int heatNumber) {
  if (!preparedAvailable_ || !snapshot.heats ||
      !heatboard::computeHeatWindow(*snapshot.heats, heatNumber).has_value()) {
    return nullptr;
  }
  if (PreparedTable* existing = findPrepared(snapshot.heats, heatNumber)) {
    return existing;
  }

  // Reuse a slot nobody needs: anything not showing the heat on screen or a neighbour of it
  // in the current list. With three slots for three wanted tables one is always free.
  const int wanted[] = {snapshot.currentHeat,
                        heatboard::nextHeatNumber(*snapshot.heats, snapshot.currentHeat).value_or(0),
                        heatboard::previousHeatNumber(*snapshot.heats, snapshot.currentHeat).value_or(0)};
  for (PreparedTable& table : prepared_) {
    bool needed = false;
    if (table.heats == snapshot.heats) {
      for (const int number : wanted) {
        needed = needed || table.heatNumber == number;
      }
    }
    if (!needed) {
      table.heats = snapshot.heats;
      table.heatNumber = heatNumber;
      table.rowsDrawn = 0;
      return &table;
    }
  }
  return nullptr;
}

void BoardScreen::drawNextRow(PreparedTable& table) {
  const auto window = heatboard::computeHeatWindow(*table.heats, table.heatNumber);
  if (!window || table.rowsDrawn >= kTableRows) {
    return;
  }
  const ui::FrameScope redirect(table.canvas);
  drawTableRow(*table.heats, *window, table.rowsDrawn);
  ++table.rowsDrawn;
}

// Draws at most one row per call so that the main loop keeps polling the touch panel while
// the neighbours of the heat on screen are being prepared.
void BoardScreen::prepareNeighbours(const AppSnapshot& snapshot) {
  if (!preparedAvailable_ || !snapshot.heats) {
    return;
  }
  const auto next = heatboard::nextHeatNumber(*snapshot.heats, snapshot.currentHeat);
  const auto previous = heatboard::previousHeatNumber(*snapshot.heats, snapshot.currentHeat);
  // Forward first: that is the direction a race day moves in.
  for (const auto& neighbour : {next, previous}) {
    if (!neighbour || *neighbour == snapshot.currentHeat) {
      continue;
    }
    PreparedTable* table = preparedFor(snapshot, *neighbour);
    if (table != nullptr && table->rowsDrawn < kTableRows) {
      drawNextRow(*table);
      return;
    }
  }
}

void BoardScreen::drawProgress() {
  const int total = app_config::kRaceTimerSeconds;
  int elapsed = timerRunning_ ? static_cast<int>((nowMs() - timerStartMs_) / 1000) : 0;
  if (elapsed > total) {
    elapsed = total;
  }

  const Rect& bar = kProgressBar;
  ui::frame().fillRect(bar.x, bar.y, bar.w, bar.h, ui::kColorBackground);
  ui::frame().fillRoundRect(bar.x, bar.y, bar.w, bar.h, 8, ui::kColorPanel);
  if (elapsed > 0) {
    const int filled = bar.w * elapsed / total;
    ui::frame().fillRoundRect(bar.x, bar.y, filled < 16 ? 16 : filled, bar.h, 8, ui::kColorBlue);
  }

  char elapsedText[8];
  char remainingText[8];
  char label[24];
  formatMinSec(elapsedText, sizeof(elapsedText), elapsed);
  formatMinSec(remainingText, sizeof(remainingText), total - elapsed);
  snprintf(label, sizeof(label), "%s / %s", elapsedText, remainingText);
  ui::drawFittedText(label, bar, 30, 20, ui::kColorText);

  drawnProgressSecond_ = elapsed;
}

void BoardScreen::drawButtons(const AppSnapshot& snapshot) {
  const bool commandable = canCommand(snapshot);
  const uint32_t disabledText = ui::kColorMutedText;
  ui::frame().fillRect(0, kStartButton.y, ui::kScreenWidth, kStartButton.h, ui::kColorBackground);

  if (timerRunning_) {
    ui::drawButton(kStartButton, "ストップ", ui::kColorRed, ui::kColorOnAccent, kButtonTextSize);
  } else {
    ui::drawButton(kStartButton, "スタート", commandable ? ui::kColorGreen : ui::kColorDisabled,
                   commandable ? ui::kColorOnAccent : disabledText, kButtonTextSize);
  }
  ui::drawButton(kPreviousButton, "前のヒート", commandable ? ui::kColorGray : ui::kColorDisabled,
                 commandable ? ui::kColorOnAccent : disabledText, kButtonTextSize);
  ui::drawButton(kNextButton, "次のヒート", commandable ? ui::kColorBlue : ui::kColorDisabled,
                 commandable ? ui::kColorOnAccent : disabledText, kButtonTextSize);

  drawnTimerRunning_ = timerRunning_;
  drawnCanCommand_ = commandable;
}

// Main screen: the heat table (previous / current / next / next+1), the race timer and the
// operator buttons.
#pragma once

#include <cstdint>
#include <string>

#include "app_state.h"
#include "canvas.h"

class BoardScreen {
 public:
  enum class Action { None, OpenSettings };

  // Full redraw, e.g. when coming back from the settings screen.
  void show(const AppSnapshot& snapshot);

  // Call every loop iteration: redraws whatever changed and runs the race timer.
  void update(const AppSnapshot& snapshot);

  Action handleTap(int x, int y, const AppSnapshot& snapshot);

  // Same shortcuts as the droras web UI: '1' start/stop, '2' previous heat, '3' next heat.
  void handleKey(char key, const AppSnapshot& snapshot);

 private:
  bool canCommand(const AppSnapshot& snapshot) const;
  void toggleStart(const AppSnapshot& snapshot);
  bool changeHeat(const AppSnapshot& snapshot, bool forward);
  void stopTimer();

  // Everything the table body depends on; it is repainted only when this changes.
  struct TableKey {
    const void* heats = nullptr;
    int currentHeat = -1;
    WifiStatus wifi = WifiStatus::Connecting;
    HeatListStatus status = HeatListStatus::Waiting;
    std::string error;

    bool operator==(const TableKey& other) const {
      return heats == other.heats && currentHeat == other.currentHeat && wifi == other.wifi &&
             status == other.status && error == other.error;
    }
  };
  static TableKey tableKeyOf(const AppSnapshot& snapshot);
  static std::string statusOf(const AppSnapshot& snapshot, uint32_t& dot);

  // The table body of one heat, drawn in advance on a canvas of its own so that switching
  // to that heat only has to copy it to the panel (~50ms) instead of drawing it first
  // (~45ms more, or over 100ms when its glyphs are not cached yet).
  struct PreparedTable {
    Canvas canvas;
    HeatListPtr heats;   // the list the rows were drawn from; null while the slot is unused
    int heatNumber = 0;  // the heat shown as current
    int rowsDrawn = 0;   // complete once every row is drawn
  };
  // One each for the heat on screen and its two neighbours.
  static constexpr int kPreparedTables = 3;

  void setUpPreparedTables();
  PreparedTable* findPrepared(const HeatListPtr& heats, int heatNumber);
  PreparedTable* preparedFor(const AppSnapshot& snapshot, int heatNumber);
  void drawNextRow(PreparedTable& table);
  void prepareNeighbours(const AppSnapshot& snapshot);

  void drawStatusBar(const AppSnapshot& snapshot);
  void drawTableHeader();
  void drawTable(const AppSnapshot& snapshot);
  void drawProgress();
  void drawButtons(const AppSnapshot& snapshot);

  bool timerRunning_ = false;
  int64_t timerStartMs_ = 0;

  // What is currently on screen, to redraw only on change.
  uint32_t drawnRevision_ = 0;
  std::string drawnStatus_;
  uint32_t drawnStatusDot_ = 0;
  TableKey drawnTable_;

  PreparedTable prepared_[kPreparedTables];
  bool preparedSetUp_ = false;
  bool preparedAvailable_ = false;  // false if their memory could not be allocated
  int drawnProgressSecond_ = -1;
  bool drawnTimerRunning_ = false;
  bool drawnCanCommand_ = false;
};

// Heat board for the M5Stack Tab5: shows the pilots of the previous, current and next two
// heats of a drone race and doubles as an operator panel for the droras server.
#include <M5Unified.h>

#include "esp_log.h"

#include "app_state.h"
#include "board_screen.h"
#include "debug_console.h"
#include "network_task.h"
#include "screen_orientation.h"
#include "settings_screen.h"
#include "settings_store.h"
#include "text_renderer.h"
#include "ui_common.h"

namespace {

const char* TAG = "main";

enum class Screen { Board, Settings };

}  // namespace

extern "C" void app_main(void) {
  auto cfg = M5.config();
  M5.begin(cfg);

  // The panel is natively portrait (720x1280); the UI is laid out for landscape.
  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  ESP_LOGI(TAG, "display=%dx%d", static_cast<int>(M5.Display.width()), static_cast<int>(M5.Display.height()));

  text::init();
  ui::init();

  // Settings are optional for operation: without NVS the board still runs on defaults.
  settings_store::init();
  ui::setVolumeLevel(settings_store::loadVolumeLevel().value_or(ui::kDefaultVolumeLevel));
  ui::setBrightnessLevel(settings_store::loadBrightnessLevel().value_or(ui::kDefaultBrightnessLevel));
  screen_orientation::init();
  debug_console::start();
  network_task::start();

  BoardScreen board;
  SettingsScreen settings;
  Screen screen = Screen::Board;
  AppSnapshot snapshot = AppState::instance().snapshot();
  board.show(snapshot);

  while (true) {
    // Before M5.update() reads the touch panel, so that a tap is mapped with the rotation
    // of the screen it is handled on.
    screen_orientation::update();
    M5.update();
    if (AppState::instance().revision() != snapshot.revision) {
      snapshot = AppState::instance().snapshot();
    }

    bool tapped = false;
    int tapX = 0;
    int tapY = 0;
    if (M5.Touch.getCount() > 0) {
      const auto& touch = M5.Touch.getDetail(0);
      if (touch.wasPressed()) {
        tapped = true;
        tapX = touch.x;
        tapY = touch.y;
      }
    }

    debug_console::Command command;
    if (debug_console::poll(command)) {
      switch (command.type) {
        case debug_console::Command::Type::Tap:
          tapped = true;
          tapX = command.x;
          tapY = command.y;
          break;
        case debug_console::Command::Type::Key:
          if (screen == Screen::Board) {
            board.handleKey(command.key, snapshot);
          }
          break;
        case debug_console::Command::Type::Screenshot:
          debug_console::dumpScreenshot();
          break;
        case debug_console::Command::Type::Imu:
          screen_orientation::logReading();
          break;
      }
    }

    if (tapped) {
      ESP_LOGI(TAG, "tap x=%d y=%d", tapX, tapY);
      if (screen == Screen::Board) {
        if (board.handleTap(tapX, tapY, snapshot) == BoardScreen::Action::OpenSettings) {
          screen = Screen::Settings;
          settings.show(snapshot);
        }
      } else if (settings.handleTap(tapX, tapY, snapshot) == SettingsScreen::Action::Close) {
        screen = Screen::Board;
        board.show(snapshot);
      }
    }

    if (screen == Screen::Board) {
      board.update(snapshot);
    } else {
      settings.update(snapshot);
    }

    M5.delay(10);
  }
}

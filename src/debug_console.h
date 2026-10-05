// Line-based control over the USB serial port, for driving and inspecting the board from a
// PC without touching it:
//   key <1|2|3>   same shortcuts as the droras web UI (start/stop, previous, next)
//   tap <x> <y>   inject a touch at screen coordinates
//   shot          dump the framebuffer (decode with tools/screenshot.py)
#pragma once

namespace debug_console {

struct Command {
  enum class Type { Key, Tap, Screenshot };
  Type type;
  char key;
  int x;
  int y;
};

void start();

// Non-blocking. Returns true and fills `command` when one is pending.
bool poll(Command& command);

// Writes the screen to the serial port. Must run on the task that owns the display.
void dumpScreenshot();

}  // namespace debug_console

// Line-based control over the USB serial port, for driving and inspecting the board from a
// PC without touching it:
//   key <1|2|3>   same shortcuts as the droras web UI (start/stop, previous, next)
//   tap <x> <y>   inject a touch at screen coordinates
//   shot          dump the framebuffer (decode with tools/screenshot.py)
//   imu           log the accelerometer reading and the screen orientation decided from it
//   camshot       dump the image the gesture models analyse (tools/screenshot.py --command camshot)
//   camtest       run the models once on the sample picture built into the firmware (pipeline check)
//   gest <g> <ms> pretend the camera sees gesture <g> (heatboard::Gesture value) for <ms> milliseconds
//   cam <exposure> <gain>   fix the camera exposure (sensor lines) and gain (index); "cam auto" resumes
//                 automatic exposure
#pragma once

namespace debug_console {

struct Command {
  enum class Type { Key, Tap, Screenshot, Imu, CameraShot, CameraExposure, CameraTest, GestureInject };
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

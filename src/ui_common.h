// Drawing and feedback helpers shared by the screens. Layout assumes the 1280x720 landscape panel.
//
// Screens never draw on the panel directly: they draw on frame(), an off-screen copy of the
// whole screen, and then present() the rows they changed, so a redraw appears all at once
// instead of as a visible top-to-bottom wipe.
#pragma once

#include <cstdint>

#include "canvas.h"

namespace ui {

inline constexpr int kScreenWidth = 1280;
inline constexpr int kScreenHeight = 720;

// 24-bit RGB. Typed uint32_t on purpose: M5GFX reads a plain int as RGB565.
inline constexpr uint32_t kColorBackground = 0xFFFFFFu;
inline constexpr uint32_t kColorText = 0x212529u;
inline constexpr uint32_t kColorMutedText = 0x6C757Du;
inline constexpr uint32_t kColorLine = 0xDEE2E6u;
inline constexpr uint32_t kColorPanel = 0xF1F3F5u;
inline constexpr uint32_t kColorCurrentRow = 0xC3E6CBu;  // the green highlight of the web UI
inline constexpr uint32_t kColorGreen = 0x28A745u;
inline constexpr uint32_t kColorRed = 0xDC3545u;
inline constexpr uint32_t kColorBlue = 0x007BFFu;
inline constexpr uint32_t kColorGray = 0x6C757Du;
inline constexpr uint32_t kColorAmber = 0xE0A800u;
inline constexpr uint32_t kColorDisabled = 0xCED4DAu;
inline constexpr uint32_t kColorOnAccent = 0xFFFFFFu;

struct Rect {
  int x;
  int y;
  int w;
  int h;

  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  int centerX() const { return x + w / 2; }
  int centerY() const { return y + h / 2; }
};

// Allocates the off-screen frame and prepares the touch sounds. Call once after M5.begin(),
// with the display already rotated to landscape. Aborts if the frame cannot be set up: the
// UI has nothing else to draw on.
void init();

// Surface to draw on.
Canvas& frame();

// Copies full-width rows [top, top + height) of the main frame to the panel.
void present(int top, int height);
void presentAll();

// Shows the UI the other way up (the Tab5 turned upside down) or back. Layout coordinates do
// not change: the frame is turned in memory and the display rotation follows it, so touch
// coordinates keep matching what is drawn. Everything is on the panel again on return.
// Canvases other than the main frame are turned by their owners (see upsideDown()).
void setUpsideDown(bool upsideDown);
bool upsideDown();

// While one of these is alive, frame() and every ui:: drawing helper draw on `canvas`
// instead of the main frame. Used to prepare a region out of sight, ahead of time.
class FrameScope {
 public:
  explicit FrameScope(Canvas& canvas);
  ~FrameScope();
  FrameScope(const FrameScope&) = delete;
  FrameScope& operator=(const FrameScope&) = delete;

 private:
  Canvas* previous_;
};

// Draws `text` centred in `area` at `maxSize` pixels, shrinking it (down to `minSize`) until
// it fits the width, so long names get smaller instead of spilling into the next column.
void drawFittedText(const char* text, const Rect& area, int maxSize, int minSize, uint32_t color);

// Draws `text` left-aligned with its top at `y`. With `clip`, nothing is drawn outside it.
void drawText(const char* text, int x, int y, int size, uint32_t color, const Rect* clip = nullptr);

// Filled rounded rectangle with a one-pixel border.
void drawOutlinedRoundRect(const Rect& area, int radius, uint32_t border, uint32_t fill);

// Rounded corners are blended into whatever is underneath. When a shape is drawn over an
// earlier one of a different colour (a button changing state), clear its area first or the
// old colour stays visible as a fringe around the corners.
void drawButton(const Rect& area, const char* label, uint32_t background, uint32_t foreground, int fontSize);

// Soft chime confirming a touch that did something / lower, duller one for a touch that
// could not.
void beepAccepted();
void beepRejected();

// Speaker volume and backlight brightness in steps of 0..kLevelMax. Values are clamped;
// brightness never goes below 1 so the screen cannot be turned fully dark from the UI.
inline constexpr int kLevelMax = 10;
inline constexpr int kDefaultVolumeLevel = 6;
inline constexpr int kDefaultBrightnessLevel = 8;
int volumeLevel();
void setVolumeLevel(int level);
int brightnessLevel();
void setBrightnessLevel(int level);

}  // namespace ui

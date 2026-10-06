// Off-screen copy of the whole screen that all drawing goes to.
//
// The Tab5 panel is physically portrait (720x1280) and M5GFX rotates every pixel written to
// it in landscape, so pushing a large image to the display costs ~0.7us per pixel (450ms
// for the heat table). This canvas therefore keeps its pixels in the panel's own memory
// layout and format (native orientation, little-endian RGB565): drawing happens in logical
// landscape coordinates, while present() is a plain row-by-row memory copy.
#pragma once

#include <cstdint>

// Half-open rectangle [x0, x1) x [y0, y1) in logical coordinates.
struct ClipRect {
  int x0;
  int y0;
  int x1;
  int y1;
};

class Canvas {
 public:
  // Works out how logical coordinates map onto panel memory and allocates the buffer.
  // Call once, after the display rotation has been chosen. Returns false (after logging)
  // if that is not possible; the canvas must not be used then.
  bool begin();

  // Sets this canvas up with the same size and layout as `other` (which must have begun).
  // For additional canvases: begin() draws probe pixels on the panel, this does not.
  bool beginLike(const Canvas& other);

  // Turns the logical coordinate system half a turn on the panel (or back), keeping what is
  // drawn: afterwards every logical pixel still holds the same colour, it is just shown
  // upside down relative to before. Call present() to put that on the panel.
  void setUpsideDown(bool upsideDown);
  bool upsideDown() const { return upsideDown_; }

  // The M5GFX rotation that matches this canvas's logical coordinates. The display is kept
  // at it so that touch coordinates and readRect() agree with what is drawn.
  uint8_t logicalRotation() const { return logicalRotation_; }

  int width() const { return logicalWidth_; }
  int height() const { return logicalHeight_; }

  // Colours are 24-bit RGB. Shapes are clipped to the canvas.
  void fillScreen(uint32_t color);
  void fillRect(int x, int y, int w, int h, uint32_t color);
  void drawFastHLine(int x, int y, int w, uint32_t color);
  // Corners are anti-aliased against what is already on the canvas.
  void fillRoundRect(int x, int y, int w, int h, int radius, uint32_t color);
  void fillCircle(int centerX, int centerY, int radius, uint32_t color);

  // Blends `color` through an 8-bit coverage map (0 = keep, 255 = replace) whose top-left
  // corner is placed at (x, y). Used for anti-aliased text. Only pixels inside `clip` change.
  void blendCoverage(const uint8_t* coverage, int w, int h, int x, int y, const ClipRect& clip, uint32_t color);

  // Copies logical rows [top, top + height) to the panel.
  void present(int top, int height);

 private:
  struct NativeRect {
    int x;
    int y;
    int w;
    int h;
  };

  bool allocate();
  int indexOf(int x, int y) const { return origin_ + x * stepX_ + y * stepY_; }
  NativeRect toNative(int x, int y, int w, int h) const;
  void blendPixel(int x, int y, uint16_t color565, int alpha);

  uint16_t* pixels_ = nullptr;
  int logicalWidth_ = 0;
  int logicalHeight_ = 0;
  int nativeWidth_ = 0;
  int nativeHeight_ = 0;
  // Buffer index of logical (0,0) and the index change for one step in logical x and y.
  int origin_ = 0;
  int stepX_ = 0;
  int stepY_ = 0;
  uint8_t logicalRotation_ = 0;
  uint8_t nativeRotation_ = 0;
  bool upsideDown_ = false;
};

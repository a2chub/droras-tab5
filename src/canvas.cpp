#include "canvas.h"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {

const char* TAG = "canvas";

constexpr uint16_t toRgb565(uint32_t color) {
  return static_cast<uint16_t>(((color >> 8) & 0xF800) | ((color >> 5) & 0x07E0) | ((color >> 3) & 0x001F));
}

// Mixes `foreground` over `background` with alpha 0..255, per RGB565 channel.
constexpr uint16_t mix565(uint16_t background, uint16_t foreground, int alpha) {
  const int br = background >> 11;
  const int bg = (background >> 5) & 0x3F;
  const int bb = background & 0x1F;
  const int fr = foreground >> 11;
  const int fg = (foreground >> 5) & 0x3F;
  const int fb = foreground & 0x1F;
  const int r = br + ((fr - br) * alpha + 127) / 255;
  const int g = bg + ((fg - bg) * alpha + 127) / 255;
  const int b = bb + ((fb - bb) * alpha + 127) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}  // namespace

bool Canvas::begin() {
  auto& display = M5.Display;
  logicalWidth_ = display.width();
  logicalHeight_ = display.height();
  logicalRotation_ = display.getRotation();

  // The rotation at which M5GFX applies no transformation, i.e. writes rows straight into
  // panel memory (see Panel_FrameBufferBase::setRotation).
  const uint8_t offset = display.getPanel()->config().offset_rotation;
  nativeRotation_ = static_cast<uint8_t>(((4 - (offset & 3)) & 3) | (offset & 4));

  // How logical coordinates land in panel memory is measured rather than assumed: plot the
  // three pixels around the logical origin, then find them again in native coordinates.
  // They can only be in the 2x2 block at one of the four native corners.
  constexpr uint32_t kOriginMark = 0xFF0000u;
  constexpr uint32_t kStepXMark = 0x00FF00u;
  constexpr uint32_t kStepYMark = 0x0000FFu;
  display.fillScreen(0x000000u);
  display.drawPixel(0, 0, kOriginMark);
  display.drawPixel(1, 0, kStepXMark);
  display.drawPixel(0, 1, kStepYMark);

  display.setRotation(nativeRotation_);
  nativeWidth_ = display.width();
  nativeHeight_ = display.height();
  int originIndex = -1;
  int stepXIndex = -1;
  int stepYIndex = -1;
  for (const int cornerX : {0, nativeWidth_ - 2}) {
    for (const int cornerY : {0, nativeHeight_ - 2}) {
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          lgfx::rgb888_t pixel;
          display.readRect(cornerX + dx, cornerY + dy, 1, 1, &pixel);
          const int index = (cornerY + dy) * nativeWidth_ + cornerX + dx;
          if (pixel.r > 128) originIndex = index;
          if (pixel.g > 128) stepXIndex = index;
          if (pixel.b > 128) stepYIndex = index;
        }
      }
    }
  }
  display.setRotation(logicalRotation_);
  display.fillScreen(0x000000u);

  if (originIndex < 0 || stepXIndex < 0 || stepYIndex < 0) {
    ESP_LOGE(TAG, "could not locate the logical origin in panel memory");
    return false;
  }
  origin_ = originIndex;
  stepX_ = stepXIndex - originIndex;
  stepY_ = stepYIndex - originIndex;
  // One axis must run along a native row and the other along a native column.
  const bool rowThenColumn = std::abs(stepX_) == 1 && std::abs(stepY_) == nativeWidth_;
  const bool columnThenRow = std::abs(stepX_) == nativeWidth_ && std::abs(stepY_) == 1;
  if (!rowThenColumn && !columnThenRow) {
    ESP_LOGE(TAG, "unexpected panel mapping: stepX=%d stepY=%d", stepX_, stepY_);
    return false;
  }

  ESP_LOGI(TAG, "logical %dx%d on native %dx%d (origin=%d stepX=%d stepY=%d)", logicalWidth_, logicalHeight_,
           nativeWidth_, nativeHeight_, origin_, stepX_, stepY_);
  return allocate();
}

bool Canvas::beginLike(const Canvas& other) {
  logicalWidth_ = other.logicalWidth_;
  logicalHeight_ = other.logicalHeight_;
  nativeWidth_ = other.nativeWidth_;
  nativeHeight_ = other.nativeHeight_;
  origin_ = other.origin_;
  stepX_ = other.stepX_;
  stepY_ = other.stepY_;
  logicalRotation_ = other.logicalRotation_;
  nativeRotation_ = other.nativeRotation_;
  upsideDown_ = other.upsideDown_;
  return other.pixels_ != nullptr && allocate();
}

bool Canvas::allocate() {
  const std::size_t bytes = static_cast<std::size_t>(nativeWidth_) * nativeHeight_ * sizeof(uint16_t);
  pixels_ = static_cast<uint16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
  if (pixels_ == nullptr) {
    ESP_LOGE(TAG, "no memory for a %u-byte frame", static_cast<unsigned>(bytes));
    return false;
  }
  fillScreen(0x000000u);
  return true;
}

void Canvas::setUpsideDown(bool upsideDown) {
  if (upsideDown == upsideDown_ || pixels_ == nullptr) {
    return;
  }
  upsideDown_ = upsideDown;
  // Half a turn of a row-major image is the same buffer read backwards: the pixel at index
  // i moves to last - i. Reversing the buffer and remapping coordinates the same way keeps
  // every logical pixel's colour, so nothing has to be drawn again.
  const int last = nativeWidth_ * nativeHeight_ - 1;
  std::reverse(pixels_, pixels_ + last + 1);
  origin_ = last - origin_;
  stepX_ = -stepX_;
  stepY_ = -stepY_;
  // M5GFX rotations 0-3 are quarter turns, so two more is the opposite landscape.
  logicalRotation_ = static_cast<uint8_t>(logicalRotation_ ^ 2);
}

Canvas::NativeRect Canvas::toNative(int x, int y, int w, int h) const {
  const int a = indexOf(x, y);
  const int b = indexOf(x + w - 1, y + h - 1);
  const int ax = a % nativeWidth_;
  const int ay = a / nativeWidth_;
  const int bx = b % nativeWidth_;
  const int by = b / nativeWidth_;
  return {std::min(ax, bx), std::min(ay, by), std::abs(ax - bx) + 1, std::abs(ay - by) + 1};
}

void Canvas::fillScreen(uint32_t color) { fillRect(0, 0, logicalWidth_, logicalHeight_, color); }

void Canvas::fillRect(int x, int y, int w, int h, uint32_t color) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, logicalWidth_);
  const int y1 = std::min(y + h, logicalHeight_);
  if (x0 >= x1 || y0 >= y1) {
    return;
  }
  // Filled along native rows so that memory is written sequentially.
  const NativeRect area = toNative(x0, y0, x1 - x0, y1 - y0);
  const uint16_t value = toRgb565(color);
  for (int row = 0; row < area.h; ++row) {
    std::fill_n(pixels_ + static_cast<std::size_t>(area.y + row) * nativeWidth_ + area.x, area.w, value);
  }
}

void Canvas::drawFastHLine(int x, int y, int w, uint32_t color) { fillRect(x, y, w, 1, color); }

void Canvas::blendPixel(int x, int y, uint16_t color565, int alpha) {
  if (alpha <= 0 || x < 0 || y < 0 || x >= logicalWidth_ || y >= logicalHeight_) {
    return;
  }
  uint16_t& pixel = pixels_[indexOf(x, y)];
  pixel = alpha >= 255 ? color565 : mix565(pixel, color565, alpha);
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int radius, uint32_t color) {
  radius = std::max(0, std::min({radius, w / 2, h / 2}));
  // The cross-shaped interior is plain rectangles; only the four corner squares need care.
  fillRect(x, y + radius, w, h - 2 * radius, color);
  fillRect(x + radius, y, w - 2 * radius, radius, color);
  fillRect(x + radius, y + h - radius, w - 2 * radius, radius, color);

  const uint16_t value = toRgb565(color);
  for (int dy = 0; dy < radius; ++dy) {
    for (int dx = 0; dx < radius; ++dx) {
      // Coverage from the distance between the pixel centre and the corner's arc centre.
      const float distance = hypotf(radius - dx - 0.5f, radius - dy - 0.5f);
      const int alpha = static_cast<int>(std::clamp(radius - distance + 0.5f, 0.0f, 1.0f) * 255.0f);
      blendPixel(x + dx, y + dy, value, alpha);
      blendPixel(x + w - 1 - dx, y + dy, value, alpha);
      blendPixel(x + dx, y + h - 1 - dy, value, alpha);
      blendPixel(x + w - 1 - dx, y + h - 1 - dy, value, alpha);
    }
  }
}

void Canvas::fillCircle(int centerX, int centerY, int radius, uint32_t color) {
  fillRoundRect(centerX - radius, centerY - radius, 2 * radius, 2 * radius, radius, color);
}

void Canvas::blendCoverage(const uint8_t* coverage, int w, int h, int x, int y, const ClipRect& clip, uint32_t color) {
  const int x0 = std::max({x, clip.x0, 0});
  const int y0 = std::max({y, clip.y0, 0});
  const int x1 = std::min({x + w, clip.x1, logicalWidth_});
  const int y1 = std::min({y + h, clip.y1, logicalHeight_});
  const uint16_t value = toRgb565(color);
  for (int py = y0; py < y1; ++py) {
    const uint8_t* source = coverage + static_cast<std::size_t>(py - y) * w + (x0 - x);
    uint16_t* target = pixels_ + indexOf(x0, py);
    for (int px = x0; px < x1; ++px, ++source, target += stepX_) {
      const int alpha = *source;
      if (alpha == 0) {
        continue;
      }
      *target = alpha == 255 ? value : mix565(*target, value, alpha);
    }
  }
}

void Canvas::present(int top, int height) {
  const int y0 = std::max(top, 0);
  const int y1 = std::min(top + height, logicalHeight_);
  if (y0 >= y1) {
    return;
  }
  const NativeRect area = toNative(0, y0, logicalWidth_, y1 - y0);

  // In its native rotation M5GFX copies same-format rows with memcpy instead of rotating
  // pixel by pixel. The rotation is restored before anything else can use the display.
  auto& display = M5.Display;
  display.setRotation(nativeRotation_);
  display.startWrite();
  if (area.w == nativeWidth_) {
    const auto* rows = reinterpret_cast<const lgfx::rgb565_t*>(pixels_ + static_cast<std::size_t>(area.y) * nativeWidth_);
    display.pushImage(0, area.y, area.w, area.h, rows);
  } else {
    // A band of logical rows is a vertical strip in panel memory: one short run per native row.
    for (int row = 0; row < area.h; ++row) {
      const auto* run = reinterpret_cast<const lgfx::rgb565_t*>(
          pixels_ + static_cast<std::size_t>(area.y + row) * nativeWidth_ + area.x);
      display.pushImage(area.x, area.y + row, area.w, 1, run);
    }
  }
  display.endWrite();
  display.setRotation(logicalRotation_);
}

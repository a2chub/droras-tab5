#include "ui_common.h"

#include <M5Unified.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <vector>

#include "esp_log.h"

#include "text_renderer.h"

namespace ui {

namespace {

constexpr int kTextPadding = 8;
constexpr int kButtonRadius = 14;
constexpr int kMinButtonTextSize = 16;

int currentVolumeLevel = kDefaultVolumeLevel;
int currentBrightnessLevel = kDefaultBrightnessLevel;

const char* TAG = "ui";

Canvas frameCanvas;
Canvas* activeCanvas = &frameCanvas;  // what frame() returns; see FrameScope

// --- Touch sounds ----------------------------------------------------------------------
// Synthesised once as PCM instead of using Speaker.tone(): a tone is a single raw waveform
// cut off abruptly, which is what made the old 2.4kHz click harsh. These are sine chords
// with a short fade-in and an exponential decay, like a soft bell.
constexpr int kSampleRate = 24000;
constexpr float kPi = 3.14159265f;

std::vector<int16_t> acceptedChime;
std::vector<int16_t> rejectedChime;

std::vector<int16_t> makeChime(std::initializer_list<float> frequencies, int durationMs, float decayMs) {
  constexpr float kAttackMs = 6.0f;
  constexpr float kPeak = 0.85f * 32767.0f;
  const int sampleCount = kSampleRate * durationMs / 1000;
  std::vector<int16_t> samples(sampleCount);
  for (int i = 0; i < sampleCount; ++i) {
    const float t = static_cast<float>(i) / kSampleRate;
    float mix = 0.0f;
    for (const float frequency : frequencies) {
      mix += sinf(2.0f * kPi * frequency * t);
    }
    mix /= static_cast<float>(frequencies.size());
    const float ms = t * 1000.0f;
    const float attack = std::min(1.0f, ms / kAttackMs);
    // The linear tail forces the last sample to zero so the end does not click either.
    const float release = static_cast<float>(sampleCount - 1 - i) / sampleCount;
    samples[i] = static_cast<int16_t>(kPeak * mix * attack * expf(-ms / decayMs) * std::min(1.0f, release * 4.0f));
  }
  return samples;
}

void play(const std::vector<int16_t>& chime) {
  if (!chime.empty()) {
    M5.Speaker.playRaw(chime.data(), chime.size(), kSampleRate, false, 1, 0, true);
  }
}

}  // namespace

void init() {
  if (!frameCanvas.begin()) {
    ESP_LOGE(TAG, "the off-screen frame could not be set up");
    abort();
  }

  // C major triad (C5, E5, G5) for "done"; a lower minor third (D4, F4) for "not possible".
  acceptedChime = makeChime({523.25f, 659.25f, 783.99f}, 240, 70.0f);
  rejectedChime = makeChime({293.66f, 349.23f}, 280, 90.0f);
}

Canvas& frame() { return *activeCanvas; }

FrameScope::FrameScope(Canvas& canvas) : previous_(activeCanvas) { activeCanvas = &canvas; }

FrameScope::~FrameScope() { activeCanvas = previous_; }

void present(int top, int height) { frameCanvas.present(top, height); }

void presentAll() { present(0, kScreenHeight); }

void setUpsideDown(bool upsideDown) {
  if (upsideDown == frameCanvas.upsideDown()) {
    return;
  }
  frameCanvas.setUpsideDown(upsideDown);
  M5.Display.setRotation(frameCanvas.logicalRotation());
  presentAll();
  ESP_LOGI(TAG, "screen %s", upsideDown ? "upside down" : "upright");
}

bool upsideDown() { return frameCanvas.upsideDown(); }

void drawFittedText(const char* text, const Rect& area, int maxSize, int minSize, uint32_t color) {
  const int size = text::fitSize(text, area.w - 2 * kTextPadding, maxSize, minSize);
  // At minSize the text may still be too wide: clipped text beats text in the next column.
  const ClipRect clip = {area.x, area.y, area.x + area.w, area.y + area.h};
  text::draw(frame(), clip, text, area.centerX(), area.centerY(), text::Align::Center, size, color);
}

void drawText(const char* text, int x, int y, int size, uint32_t color, const Rect* clip) {
  const ClipRect bounds = clip != nullptr ? ClipRect{clip->x, clip->y, clip->x + clip->w, clip->y + clip->h}
                                          : ClipRect{0, 0, kScreenWidth, kScreenHeight};
  text::draw(frame(), bounds, text, x, y + size / 2, text::Align::Left, size, color);
}

void drawButton(const Rect& area, const char* label, uint32_t background, uint32_t foreground, int fontSize) {
  frame().fillRoundRect(area.x, area.y, area.w, area.h, kButtonRadius, background);
  drawFittedText(label, area, fontSize, kMinButtonTextSize, foreground);
}

void drawOutlinedRoundRect(const Rect& area, int radius, uint32_t border, uint32_t fill) {
  frame().fillRoundRect(area.x, area.y, area.w, area.h, radius, border);
  frame().fillRoundRect(area.x + 1, area.y + 1, area.w - 2, area.h - 2, radius - 1, fill);
}

void beepAccepted() { play(acceptedChime); }

void beepRejected() { play(rejectedChime); }

int volumeLevel() { return currentVolumeLevel; }

void setVolumeLevel(int level) {
  currentVolumeLevel = std::clamp(level, 0, kLevelMax);
  M5.Speaker.setVolume(static_cast<uint8_t>(currentVolumeLevel * 255 / kLevelMax));
}

int brightnessLevel() { return currentBrightnessLevel; }

void setBrightnessLevel(int level) {
  currentBrightnessLevel = std::clamp(level, 1, kLevelMax);
  M5.Display.setBrightness(static_cast<uint8_t>(currentBrightnessLevel * 255 / kLevelMax));
}

}  // namespace ui

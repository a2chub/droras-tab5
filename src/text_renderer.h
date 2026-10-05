// Anti-aliased text from the embedded TrueType font (IPAex Gothic), at any pixel size.
//
// The bitmap fonts bundled with M5GFX stop at JIS level 1 and 40 pixels; pilot names need
// level-2 kanji (邉, 螻, 髙, ...) and should be as large as the layout allows.
// All functions must be called from one task (the UI task): the rasteriser keeps shared state.
#pragma once

#include <cstdint>

#include "canvas.h"

namespace text {

enum class Align { Left, Center };

// Loads the font. Returns false (after logging) if it cannot be opened; drawing calls are
// then no-ops rather than crashes.
bool init();

// Width in pixels of UTF-8 `utf8` set at `pixelSize`.
int width(const char* utf8, int pixelSize);

// Largest size within [minSize, maxSize] at which `utf8` is at most `maxWidth` pixels wide
// (minSize if even that is too wide).
int fitSize(const char* utf8, int maxWidth, int maxSize, int minSize);

// Draws one line onto `canvas`, vertically centred on `centerY` and anchored at `x` according
// to `align`. The text is blended over what the canvas already holds, in 24-bit RGB `color`,
// and only inside `clip`.
void draw(Canvas& canvas, const ClipRect& clip, const char* utf8, int x, int centerY, Align align, int pixelSize,
          uint32_t color);

}  // namespace text

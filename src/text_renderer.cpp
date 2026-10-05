#include "text_renderer.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H

#include "esp_log.h"

// Linked in by EMBED_FILES in src/CMakeLists.txt.
extern const uint8_t fontFileStart[] asm("_binary_ipaexg_ttf_start");
extern const uint8_t fontFileEnd[] asm("_binary_ipaexg_ttf_end");

namespace text {

namespace {

const char* TAG = "text";

FT_Library library = nullptr;
FT_Face face = nullptr;
int loadedPixelSize = 0;  // size the face is currently scaled to

// --- Glyph cache -----------------------------------------------------------------------
// Rasterising costs about 0.3ms per glyph, and a heat change repaints over a hundred of
// them, most of which were on screen a moment ago at the same size (a heat keeps its size
// while it moves between the small rows). Rendered bitmaps are therefore kept, keyed by
// glyph and pixel size.
struct CachedGlyph {
  uint32_t bitmapOffset;  // into glyphBitmaps
  uint16_t width;
  uint16_t rows;
  int16_t left;           // bitmap origin relative to the pen position / baseline
  int16_t top;
  FT_Fixed advance;       // 16.16 pixels, unhinted
};

// Enough for every name of a race day at a few sizes. When it runs out the cache simply
// starts over: correctness never depends on a hit.
constexpr std::size_t kCacheBytes = 2 * 1024 * 1024;
constexpr int kMaxPixelSize = 255;

std::unordered_map<uint32_t, CachedGlyph> glyphs;
std::vector<uint8_t> glyphBitmaps;

// Reused between calls so that drawing a table of names does not hammer the allocator.
std::vector<uint8_t> coverage;

// Decodes one code point and advances `p`. Malformed bytes decode to U+FFFD one at a time,
// so bad input shows as missing glyphs instead of derailing the rest of the string.
uint32_t nextCodePoint(const uint8_t*& p) {
  const uint8_t lead = *p++;
  if (lead < 0x80) {
    return lead;
  }
  int extra = 0;
  uint32_t codePoint = 0;
  if ((lead & 0xE0) == 0xC0) {
    extra = 1;
    codePoint = lead & 0x1F;
  } else if ((lead & 0xF0) == 0xE0) {
    extra = 2;
    codePoint = lead & 0x0F;
  } else if ((lead & 0xF8) == 0xF0) {
    extra = 3;
    codePoint = lead & 0x07;
  } else {
    return 0xFFFD;
  }
  for (int i = 0; i < extra; ++i) {
    if ((*p & 0xC0) != 0x80) {
      return 0xFFFD;
    }
    codePoint = (codePoint << 6) | (*p++ & 0x3F);
  }
  return codePoint;
}

// Width in font design units. Advances are taken unscaled and unhinted so that one
// measurement serves every pixel size (fitSize) and matches the pen positions used by draw().
int64_t widthInFontUnits(const char* utf8) {
  int64_t total = 0;
  const auto* p = reinterpret_cast<const uint8_t*>(utf8);
  while (*p != 0) {
    FT_Fixed advance = 0;
    if (FT_Get_Advance(face, FT_Get_Char_Index(face, nextCodePoint(p)), FT_LOAD_NO_SCALE, &advance) == 0) {
      total += advance;
    }
  }
  return total;
}

// Rounds a length in font units up to whole pixels at `pixelSize`.
int unitsToPixels(int64_t units, int pixelSize) {
  return static_cast<int>((units * pixelSize + face->units_per_EM - 1) / face->units_per_EM);
}

// Returns the rendered glyph, rasterising it on a cache miss. nullptr if it cannot be
// rendered. The pointer is only valid until the next call.
const CachedGlyph* glyphFor(FT_UInt glyphIndex, int pixelSize) {
  const uint32_t key = (static_cast<uint32_t>(glyphIndex) << 8) | static_cast<uint32_t>(pixelSize);
  const auto found = glyphs.find(key);
  if (found != glyphs.end()) {
    return &found->second;
  }

  if (loadedPixelSize != pixelSize) {
    if (FT_Set_Pixel_Sizes(face, 0, pixelSize) != 0) {
      return nullptr;
    }
    loadedPixelSize = pixelSize;
  }
  // Light hinting snaps stems vertically only, which keeps kanji crisp without distorting
  // the advance widths the layout was measured with.
  if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0) {
    return nullptr;
  }
  const FT_GlyphSlot slot = face->glyph;
  const FT_Bitmap& bitmap = slot->bitmap;
  const std::size_t bytes = static_cast<std::size_t>(bitmap.width) * bitmap.rows;
  if (glyphBitmaps.size() + bytes > kCacheBytes) {
    ESP_LOGI(TAG, "glyph cache full (%u glyphs), starting over", static_cast<unsigned>(glyphs.size()));
    glyphs.clear();
    glyphBitmaps.clear();
  }

  CachedGlyph glyph = {};
  glyph.bitmapOffset = static_cast<uint32_t>(glyphBitmaps.size());
  glyph.width = static_cast<uint16_t>(bitmap.width);
  glyph.rows = static_cast<uint16_t>(bitmap.rows);
  glyph.left = static_cast<int16_t>(slot->bitmap_left);
  glyph.top = static_cast<int16_t>(slot->bitmap_top);
  glyph.advance = slot->linearHoriAdvance;
  // Stored without row padding: FreeType's pitch can be wider than the bitmap.
  for (unsigned row = 0; row < bitmap.rows; ++row) {
    const uint8_t* source = bitmap.buffer + static_cast<std::ptrdiff_t>(row) * bitmap.pitch;
    glyphBitmaps.insert(glyphBitmaps.end(), source, source + bitmap.width);
  }
  return &glyphs.insert_or_assign(key, glyph).first->second;
}

}  // namespace

bool init() {
  if (FT_Init_FreeType(&library) != 0) {
    ESP_LOGE(TAG, "FT_Init_FreeType failed");
    return false;
  }
  // The font stays in flash: FreeType reads the memory-mapped file in place.
  const FT_Error error =
      FT_New_Memory_Face(library, fontFileStart, static_cast<FT_Long>(fontFileEnd - fontFileStart), 0, &face);
  if (error != 0) {
    ESP_LOGE(TAG, "FT_New_Memory_Face failed: %d", static_cast<int>(error));
    face = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "font loaded: %s, %ld glyphs", face->family_name, static_cast<long>(face->num_glyphs));
  return true;
}

int width(const char* utf8, int pixelSize) {
  if (face == nullptr) {
    return 0;
  }
  return unitsToPixels(widthInFontUnits(utf8), pixelSize);
}

int fitSize(const char* utf8, int maxWidth, int maxSize, int minSize) {
  if (face == nullptr) {
    return minSize;
  }
  const int64_t units = widthInFontUnits(utf8);
  if (units <= 0) {
    return maxSize;
  }
  const int fitting = static_cast<int>(static_cast<int64_t>(maxWidth) * face->units_per_EM / units);
  return std::clamp(fitting, minSize, maxSize);
}

void draw(Canvas& canvas, const ClipRect& clip, const char* utf8, int x, int centerY, Align align, int pixelSize,
          uint32_t color) {
  if (face == nullptr || utf8[0] == '\0' || pixelSize < 1 || pixelSize > kMaxPixelSize) {
    return;
  }

  // One pixel of slack on each side: hinted glyphs may reach slightly past their advance.
  constexpr int kMargin = 1;
  // Line box from the font's own metrics, so every string at a size sits on the same baseline.
  const int ascender = unitsToPixels(face->ascender, pixelSize);
  const int descender = unitsToPixels(-face->descender, pixelSize);
  const int w = width(utf8, pixelSize) + 2 * kMargin;
  const int h = ascender + descender;
  if (w <= 2 * kMargin || h <= 0) {
    return;
  }

  // The whole line is composed in one coverage map first: glyph boxes of neighbouring
  // characters can overlap, and blending them one by one would darken the overlap twice.
  coverage.assign(static_cast<std::size_t>(w) * h, 0);
  FT_Fixed pen = 0;  // 16.16 pixels
  const auto* p = reinterpret_cast<const uint8_t*>(utf8);
  while (*p != 0) {
    const CachedGlyph* glyph = glyphFor(FT_Get_Char_Index(face, nextCodePoint(p)), pixelSize);
    if (glyph == nullptr) {
      continue;
    }
    const uint8_t* bitmap = glyphBitmaps.data() + glyph->bitmapOffset;
    const int left = kMargin + static_cast<int>((pen + 0x8000) >> 16) + glyph->left;
    const int top = ascender - glyph->top;
    for (int row = 0; row < glyph->rows; ++row) {
      const int y = top + row;
      if (y < 0 || y >= h) {
        continue;
      }
      const uint8_t* source = bitmap + static_cast<std::size_t>(row) * glyph->width;
      uint8_t* target = coverage.data() + static_cast<std::size_t>(y) * w;
      for (int column = 0; column < glyph->width; ++column) {
        const int tx = left + column;
        if (tx >= 0 && tx < w) {
          target[tx] = std::max(target[tx], source[column]);
        }
      }
    }
    pen += glyph->advance;
  }

  const int left = (align == Align::Center ? x - w / 2 : x - kMargin);
  canvas.blendCoverage(coverage.data(), w, h, left, centerY - h / 2, clip, color);
}

}  // namespace text

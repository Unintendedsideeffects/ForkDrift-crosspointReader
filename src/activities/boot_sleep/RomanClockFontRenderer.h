#pragma once

#include <GfxRenderer.h>
#include <Utf8.h>

#include <algorithm>
#include <string>
#include <vector>

#include "fontIds.h"

namespace RomanClockFontRenderer {

struct LabelParts {
  std::string hour;
  std::string minute;
};

inline LabelParts splitLabel(const std::string& label) {
  LabelParts parts;
  const size_t separator = label.find(':');
  if (separator == std::string::npos) {
    parts.hour = label;
    return parts;
  }

  parts.hour = label.substr(0, separator);
  parts.minute = label.substr(separator + 1);
  return parts;
}

constexpr int FONT_ID = UI_12_FONT_ID;
constexpr EpdFontFamily::Style FONT_STYLE = EpdFontFamily::BOLD;

inline const EpdFontFamily* getFontFamily(const GfxRenderer& renderer) {
  const auto& fontMap = renderer.getFontMap();
  const auto fontIt = fontMap.find(FONT_ID);
  return fontIt == fontMap.end() ? nullptr : &fontIt->second;
}

inline const EpdFontData* getFontData(const GfxRenderer& renderer) {
  const EpdFontFamily* family = getFontFamily(renderer);
  return family ? family->getData(FONT_STYLE) : nullptr;
}

inline int baseTextWidth(const GfxRenderer& renderer, const std::string& text) {
  if (text.empty()) {
    return 0;
  }

  const EpdFontFamily* family = getFontFamily(renderer);
  if (family == nullptr) {
    return 0;
  }

  const char* textPtr = text.c_str();
  uint32_t cp = 0;
  uint32_t prevCp = 0;
  int widthPx = 0;
  int32_t prevAdvanceFP = 0;

  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&textPtr)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }

    cp = family->applyLigatures(cp, textPtr, FONT_STYLE);
    if (prevCp != 0) {
      widthPx += fp4::toPixel(prevAdvanceFP + family->getKerning(prevCp, cp, FONT_STYLE));
    }

    const EpdGlyph* glyph = family->getGlyph(cp, FONT_STYLE);
    prevAdvanceFP = glyph ? glyph->advanceX : 0;
    prevCp = cp;
  }

  widthPx += fp4::toPixel(prevAdvanceFP);
  return widthPx;
}

inline int baseTextHeight(const GfxRenderer& renderer) {
  const EpdFontData* fontData = getFontData(renderer);
  return fontData ? fontData->ascender : 0;
}

inline int scaledTextWidth(const GfxRenderer& renderer, const std::string& text, int scale) {
  return baseTextWidth(renderer, text) * scale;
}

inline int fitTextScale(const GfxRenderer& renderer, const std::string& text, int maxWidth, int maxHeight) {
  if (text.empty()) {
    return 0;
  }

  const int width = baseTextWidth(renderer, text);
  const int height = baseTextHeight(renderer);
  if (width <= 0 || height <= 0) {
    return 0;
  }

  return std::max(1, std::min(maxWidth / width, maxHeight / height));
}

// Splits a Roman numeral hour string into at most two rows using a balanced
// split: keeps subtractive pairs (IV, IX, …) intact and chooses the split
// point that minimises |len(row1) - len(row2)|.  Strings of ≤3 chars are
// returned as a single row.
inline std::vector<std::string> splitHourIntoRows(const std::string& hour) {
  if (hour.size() <= 3) {
    return {hour};
  }

  static constexpr const char* kSubtractivePairs[] = {"IV", "IX", "XL", "XC", "CD", "CM", nullptr};

  // Tokenise, keeping two-char subtractive pairs as single tokens.
  std::vector<std::string> tokens;
  size_t i = 0;
  while (i < hour.size()) {
    if (i + 1 < hour.size()) {
      const std::string pair = hour.substr(i, 2);
      bool isSubtractive = false;
      for (int k = 0; kSubtractivePairs[k]; ++k) {
        if (pair == kSubtractivePairs[k]) {
          isSubtractive = true;
          break;
        }
      }
      if (isSubtractive) {
        tokens.push_back(pair);
        i += 2;
        continue;
      }
    }
    tokens.push_back(std::string(1, hour[i]));
    ++i;
  }

  // Build prefix character-lengths so we can evaluate every split point.
  std::vector<int> prefix(tokens.size() + 1, 0);
  for (size_t j = 0; j < tokens.size(); ++j) {
    prefix[j + 1] = prefix[j] + static_cast<int>(tokens[j].size());
  }
  const int total = prefix[tokens.size()];

  // Find the split that minimises |len(row1) - len(row2)|.
  int bestDiff = total + 1;
  int bestSplit = static_cast<int>(tokens.size()) / 2;
  for (int split = 1; split < static_cast<int>(tokens.size()); ++split) {
    const int diff = std::abs(prefix[split] - (total - prefix[split]));
    if (diff < bestDiff) {
      bestDiff = diff;
      bestSplit = split;
    }
  }

  std::string row1, row2;
  for (int j = 0; j < bestSplit; ++j) row1 += tokens[j];
  for (int j = bestSplit; j < static_cast<int>(tokens.size()); ++j) row2 += tokens[j];
  return {row1, row2};
}

// Returns the largest integer scale such that the widest row fits in maxWidth
// and all rows stacked fit in maxHeight.
inline int fitMultiRowScale(const GfxRenderer& renderer, const std::vector<std::string>& rows, int maxWidth,
                            int maxHeight) {
  if (rows.empty()) return 0;

  int maxRowBaseW = 0;
  for (const auto& row : rows) {
    maxRowBaseW = std::max(maxRowBaseW, baseTextWidth(renderer, row));
  }

  const int rowBaseH = baseTextHeight(renderer);
  const int totalBaseH = rowBaseH * static_cast<int>(rows.size());
  if (maxRowBaseW <= 0 || totalBaseH <= 0) return 0;

  return std::max(1, std::min(maxWidth / maxRowBaseW, maxHeight / totalBaseH));
}

inline bool drawScaledText(GfxRenderer& renderer, const std::string& text, int x, int y, int scale, bool black = true);

// Draws rows stacked and centred within the box (cx, cy, availW, availH).
inline bool drawMultiRowText(GfxRenderer& renderer, const std::vector<std::string>& rows, int cx, int cy, int availW,
                             int availH, int scale) {
  if (rows.empty() || scale <= 0) return false;

  const int rowH = baseTextHeight(renderer) * scale;
  const int totalH = rowH * static_cast<int>(rows.size());
  int y = cy + (availH - totalH) / 2;

  for (const auto& row : rows) {
    const int rowW = scaledTextWidth(renderer, row, scale);
    const int x = cx + (availW - rowW) / 2;
    if (!drawScaledText(renderer, row, x, y, scale)) return false;
    y += rowH;
  }
  return true;
}

inline void drawScaledGlyph(GfxRenderer& renderer, const EpdFontFamily& fontFamily, const EpdFontData* fontData,
                            const EpdGlyph* glyph, int cursorX, int baselineY, int scale, bool black = true) {
  const uint8_t* bitmap = renderer.getGlyphBitmap(fontData, glyph);
  if (bitmap == nullptr) {
    return;
  }

  const int left = cursorX + glyph->left * scale;
  const int top = baselineY - glyph->top * scale;
  const bool is2Bit = fontData->is2Bit;

  if (is2Bit) {
    int pixelPosition = 0;
    for (int glyphY = 0; glyphY < glyph->height; ++glyphY) {
      for (int glyphX = 0; glyphX < glyph->width; ++glyphX, ++pixelPosition) {
        const uint8_t byte = bitmap[pixelPosition >> 2];
        const uint8_t bitIndex = (3 - (pixelPosition & 3)) * 2;
        const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);
        if (bmpVal < 3) {
          renderer.fillRect(left + glyphX * scale, top + glyphY * scale, scale, scale, black);
        }
      }
    }
    return;
  }

  int pixelPosition = 0;
  for (int glyphY = 0; glyphY < glyph->height; ++glyphY) {
    for (int glyphX = 0; glyphX < glyph->width; ++glyphX, ++pixelPosition) {
      const uint8_t byte = bitmap[pixelPosition >> 3];
      const uint8_t bitIndex = 7 - (pixelPosition & 7);
      if ((byte >> bitIndex) & 1) {
        renderer.fillRect(left + glyphX * scale, top + glyphY * scale, scale, scale, black);
      }
    }
  }
}

inline bool drawScaledText(GfxRenderer& renderer, const std::string& text, int x, int y, int scale, bool black) {
  if (text.empty()) {
    return true;
  }

  const EpdFontFamily* fontFamily = getFontFamily(renderer);
  const EpdFontData* fontData = getFontData(renderer);
  if (fontFamily == nullptr || fontData == nullptr) {
    return false;
  }

  const char* textPtr = text.c_str();
  const int baselineY = y + fontData->ascender * scale;
  int lastBaseX = x;
  uint32_t prevCp = 0;
  int32_t prevAdvanceFP = 0;

  uint32_t cp = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&textPtr)))) {
    if (utf8IsCombiningMark(cp)) {
      continue;
    }

    cp = fontFamily->applyLigatures(cp, textPtr, FONT_STYLE);
    if (prevCp != 0) {
      const auto kernFP = fontFamily->getKerning(prevCp, cp, FONT_STYLE);
      lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP) * scale;
    }

    const EpdGlyph* glyph = fontFamily->getGlyph(cp, FONT_STYLE);
    prevAdvanceFP = glyph ? glyph->advanceX : 0;
    if (glyph != nullptr) {
      drawScaledGlyph(renderer, *fontFamily, fontData, glyph, lastBaseX, baselineY, scale, black);
    }
    prevCp = cp;
  }

  return true;
}

}  // namespace RomanClockFontRenderer

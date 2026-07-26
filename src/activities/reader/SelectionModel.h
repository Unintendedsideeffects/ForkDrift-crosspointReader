#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace selection {

struct SelWord {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  uint16_t lineId = 0;
  std::string text;
};

struct HighlightRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct PageGenerationKey {
  uintptr_t book = 0;
  int32_t spine = -1;
  int32_t page = -1;
  int16_t marginTop = 0;
  int16_t marginRight = 0;
  int16_t marginBottom = 0;
  int16_t marginLeft = 0;
  int16_t screenWidth = 0;
  int16_t screenHeight = 0;
  int16_t fontId = 0;
  uint8_t orientation = 0;
  float lineCompression = 0.0f;
  uint8_t extraParagraphSpacing = 0;
  uint8_t forceParagraphIndents = 0;
  uint8_t paragraphAlignment = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t embeddedStyle = 0;
  uint8_t imageRendering = 0;
  uint8_t focusReadingEnabled = 0;
  uint8_t guideReadingEnabled = 0;

  bool operator==(const PageGenerationKey& other) const {
    return book == other.book && spine == other.spine && page == other.page && marginTop == other.marginTop &&
           marginRight == other.marginRight && marginBottom == other.marginBottom && marginLeft == other.marginLeft &&
           screenWidth == other.screenWidth && screenHeight == other.screenHeight && fontId == other.fontId &&
           orientation == other.orientation && lineCompression == other.lineCompression &&
           extraParagraphSpacing == other.extraParagraphSpacing &&
           forceParagraphIndents == other.forceParagraphIndents && paragraphAlignment == other.paragraphAlignment &&
           hyphenationEnabled == other.hyphenationEnabled && embeddedStyle == other.embeddedStyle &&
           imageRendering == other.imageRendering && focusReadingEnabled == other.focusReadingEnabled &&
           guideReadingEnabled == other.guideReadingEnabled;
  }
};

enum class FrameOrientation : uint8_t {
  Portrait = 0,
  LandscapeClockwise = 1,
  PortraitInverted = 2,
  LandscapeCounterClockwise = 3
};

enum class IncrementalDamageStatus : uint8_t { NoChange, Applied, FullRedrawRequired };

struct Model {
  std::vector<SelWord> words;
  int cursor = 0;
  int anchor = 0;
  bool anchored = false;
};

void move(Model& model, int delta);
bool stepBack(Model& model);
std::pair<int, int> span(const Model& model);
std::string joinSpan(const std::vector<SelWord>& words, const int lo, const int hi);
std::string joinSpanFormatted(const std::vector<SelWord>& words, int lo, int hi);
// Reconstructs the sentence surrounding the selected span [lo, hi] from the
// page word list. Expands left/right to sentence boundaries (a word ending in
// '.', '!' or '?'), then joins whole words up to the soft `maxChars` cap. The
// selected span is always retained, even if it alone exceeds the cap, and no
// UTF-8 codepoint is split.
std::string sentenceSpan(const std::vector<SelWord>& words, int lo, int hi, size_t maxChars = 240);
bool anchorByText(const std::vector<SelWord>& words, const std::string& text, int hintLo, int hintHi, int& outLo,
                  int& outHi);
// Like anchorByText's slide search, but reports whether the text occurs more
// than once among `words`. Returns false when there is no match at all.
// `unique` is set true only when exactly one occurrence exists.
bool anchorByTextUnique(const std::vector<SelWord>& words, const std::string& text, int spanLen, int& outLo, int& outHi,
                        bool& unique);
std::vector<HighlightRect> buildHighlightRuns(const std::vector<SelWord>& words, int lo, int hi);
// Reuses caller-owned capacity. Once `runs` has been reserved for the current
// page, warm cursor moves do not allocate.
bool buildHighlightRuns(const std::vector<SelWord>& words, int lo, int hi, std::vector<HighlightRect>& runs);

bool highlightRectsIntersect(const HighlightRect& lhs, const HighlightRect& rhs);
void invertHighlightRect(uint8_t* frameBuffer, size_t frameBufferSize, uint16_t panelWidth, uint16_t panelHeight,
                         FrameOrientation orientation, const HighlightRect& rect);
IncrementalDamageStatus applyIncrementalSelectionOverlay(uint8_t* frameBuffer, const uint8_t* baseSnapshot,
                                                         size_t frameBufferSize, uint16_t panelWidth,
                                                         uint16_t panelHeight, FrameOrientation orientation,
                                                         const std::vector<HighlightRect>& previousRuns,
                                                         const std::vector<HighlightRect>& currentRuns);

}  // namespace selection

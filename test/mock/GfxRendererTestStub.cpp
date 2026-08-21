// Host-test stand-in for lib/GfxRenderer/GfxRenderer.cpp.
//
// ParsedText::layoutAndExtractLines() only needs GfxRenderer for text
// measurement (getTextAdvanceX/getSpaceWidth/getSpaceAdvance/getKerning),
// SD-card-font gating (isSdCardFont(), already inline and false here since
// sdCardFonts_ stays empty), and getLineHeight(). The real GfxRenderer.cpp
// pulls in FontCacheManager, SdCardFont, and the E-Ink drawing path -- none
// of which layout code touches -- so linking it here would drag the whole
// display stack into a text-layout test for no reason.
//
// Measurements are deterministic byte/codepoint counts rather than real font
// metrics: tests assert on exact pixel arithmetic (e.g. "these two fragments
// are adjacent with zero gap"), which only works if the stub's arithmetic is
// simple enough to predict by hand.
#include <GfxRenderer.h>
#include <Utf8.h>

namespace {
constexpr int kPixelsPerCodepoint = 10;
constexpr int kSpaceWidth = 6;

int codepointCount(const char* text) {
  int count = 0;
  const auto* ptr = reinterpret_cast<const unsigned char*>(text);
  while (*ptr) {
    if (utf8NextCodepoint(&ptr) == 0) break;
    count++;
  }
  return count;
}
}  // namespace

void GfxRenderer::freeBwBufferChunks() {
  for (auto* chunk : bwBufferChunks) {
    delete[] chunk;
  }
  bwBufferChunks.clear();
}

void GfxRenderer::ensureSdCardFontReady(int, const char*, uint8_t) const {}

void GfxRenderer::ensureSdCardFontReady(int, const std::vector<std::string>&, bool, uint8_t) const {}

int GfxRenderer::getSpaceWidth(int, EpdFontFamily::Style) const { return kSpaceWidth; }

int GfxRenderer::getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return kSpaceWidth; }

int GfxRenderer::getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }

int GfxRenderer::getTextAdvanceX(int, const char* text, EpdFontFamily::Style) const {
  return codepointCount(text) * kPixelsPerCodepoint;
}

int GfxRenderer::getLineHeight(int) const { return 20; }

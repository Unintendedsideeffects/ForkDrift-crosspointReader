#pragma once

#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/Logo160.h"

namespace BrandScreen {

inline constexpr uint16_t kLogoSize = 160;
inline constexpr int kTitleOffsetFromCenter = 90;
inline constexpr int kSubtitleOffsetFromCenter = 115;

inline void drawLogo(const GfxRenderer& renderer, const int pageWidth, const int pageHeight) {
  renderer.drawImage(Logo160, (pageWidth - kLogoSize) / 2, (pageHeight - kLogoSize) / 2, kLogoSize, kLogoSize);
}

inline void drawTitle(const GfxRenderer& renderer, const int pageHeight) {
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + kTitleOffsetFromCenter, "ForkDrift", true,
                            EpdFontFamily::BOLD);
}

inline void drawSubtitle(const GfxRenderer& renderer, const int pageHeight, const char* text) {
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + kSubtitleOffsetFromCenter, text, true);
}

}  // namespace BrandScreen

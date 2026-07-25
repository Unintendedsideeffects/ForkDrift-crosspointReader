#pragma once

// STATUS: NON-FUNCTIONAL — pending rework.
// The Flow theme is disabled in all build profiles (ENABLE_FLOW_THEME=0) and is not
// offered in the settings picker or the web configurator. The code is retained
// deliberately: this theme is to be reworked, not removed. UI_THEME::FLOW = 8 stays
// reserved so stored settings from older firmware still round-trip.

#include <FeatureFlags.h>
#if ENABLE_FLOW_THEME

#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace FlowMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  v.homeContentTopOffset = 0;
  v.topPadding = 5;
  v.listRowHeight = 45;
  v.listWithSubtitleRowHeight = 75;
  v.menuRowHeight = 56;
  v.menuSpacing = 8;
  v.tabSpacing = 12;
  v.homeCoverHeight = 320;
  v.homeCoverTileHeight = 380;
  v.homeRecentBooksCount = 7;
  v.homeUsesCarouselCache = true;
  v.keyboardKeyHeight = 50;
  v.keyboardCenteredText = true;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace FlowMetrics

class FlowTheme : public BaseTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, float progressPercent = -1.0f) const override;

  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;

  void drawFooter(GfxRenderer& renderer) const;
};

#endif

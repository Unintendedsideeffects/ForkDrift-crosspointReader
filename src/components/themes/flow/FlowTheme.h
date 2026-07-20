#pragma once

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

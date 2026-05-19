#pragma once

#include "components/themes/lyra/LyraTheme.h"

// Minimal theme: a stripped-back, single-cover home screen built on top of
// LyraTheme. Ported from CrossInk (uxjulia/CrossInk, commit 91f11a03), adapted
// to ForkDrift's BaseTheme vtable: the upstream isHeader / allowInvertedText /
// reading-stats-on-cover couplings are deferred to their own port commits, so
// those overrides match ForkDrift's current signatures exactly.
namespace MinimalMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics v = LyraMetrics::values;
  v.homeTopPadding = 50;
  v.homeCoverHeight = 583;
  v.homeCoverTileHeight = 690;
  v.homeRecentBooksCount = 1;
  v.homeContinueReadingInMenu = false;
  v.homeMenuTopOffset = 0;
  return v;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace MinimalMetrics

class MinimalTheme : public LyraTheme {
 public:
  static void setHomeButtonHintSelection(int selectedIndex);

  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer,
                           float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};

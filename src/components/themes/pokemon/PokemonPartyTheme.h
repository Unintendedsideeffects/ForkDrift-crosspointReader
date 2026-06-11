#pragma once

#include <FeatureFlags.h>

#if ENABLE_POKEMON_PARTY

#include "components/themes/lyra/ForkDriftTheme.h"

class GfxRenderer;

namespace PokemonPartyMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = ForkDriftMetrics::values;
  v.homeRecentBooksCount = 6;
  v.homeNavigationMode = HomeNavigationMode::CoverGridDualFocus;
  // One column of six slots: up/down walks the featured slot + rows in visual
  // order, down from the last row drops into the icon menu.
  v.homeCoverGridColumns = 1;
  v.homeCoverGridRows = 6;
  // Tall enough that the party area is only bounded by the menu strip; the
  // render path clamps to usablePageHeight - menuMinH.
  v.homeCoverTileHeight = 720;
  v.homeStartInMenuWhenEmpty = true;
  // Single carousel-style icon row (icons + selected label) instead of tiles:
  // 32px icon + 2*6 pad + 4 gap + ~16px label line.
  v.menuRowHeight = 68;
  v.verticalSpacing = 8;
  v.statusBarVerticalMargin = 30;
  v.homeMenuColumns = 8;
  return v;
}();
}  // namespace PokemonPartyMetrics

class PokemonPartyTheme : public ForkDriftTheme {
 public:
  static constexpr int kCoverIconSize = 56;
  static void invalidateCache();
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};

#endif  // ENABLE_POKEMON_PARTY

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
  v.homeCoverGridColumns = 2;
  v.homeCoverGridRows = 3;
  v.homeStartInMenuWhenEmpty = true;
  v.menuRowHeight = 92;
  v.verticalSpacing = 8;
  v.statusBarVerticalMargin = 30;
  v.homeMenuColumns = 2;
  return v;
}();
}  // namespace PokemonPartyMetrics

class PokemonPartyTheme : public ForkDriftTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, float progressPercent = -1.0f) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool allowInvertedText = false) const override;
};

#endif  // ENABLE_POKEMON_PARTY

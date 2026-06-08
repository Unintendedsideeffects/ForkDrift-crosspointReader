#pragma once

#include <FeatureFlags.h>

#if ENABLE_POKEMON_PARTY

#include "components/themes/lyra/ForkDriftTheme.h"

class GfxRenderer;

// Pokémon Party home theme — a FireRed/LeafGreen-style team menu where the six
// most recent books are the party. Each slot shows the book's assigned Pokémon
// (sprite + species), its level (driven by reading progress), and an HP-style
// progress bar. Reuses ForkDrift's 6-slot grid navigation but stacks the slots
// vertically (1 column) like the GBA party screen.
//
// Compiled in only when ENABLE_POKEMON_PARTY is set (see the #if above and the
// guarded include in UITheme.cpp).
namespace PokemonPartyMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = ForkDriftMetrics::values;
  v.homeRecentBooksCount = 6;
  v.homeNavigationMode = HomeNavigationMode::CoverGridDualFocus;  // shared cover-grid nav
  v.homeCoverGridColumns = 1;                                     // single vertical column of party slots
  v.homeCoverGridRows = 6;
  v.homeStartInMenuWhenEmpty = true;
  return v;
}();
}  // namespace PokemonPartyMetrics

class PokemonPartyTheme : public ForkDriftTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, float progressPercent = -1.0f) const override;
};

#endif  // ENABLE_POKEMON_PARTY

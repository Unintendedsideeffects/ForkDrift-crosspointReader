#pragma once

#include <FeatureFlags.h>

#include "components/themes/lyra/ForkDriftTheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#if ENABLE_POKEMON_PARTY
#include "components/themes/pokemon/PokemonPartyTheme.h"
#endif

namespace coverthumbs {

struct Size {
  int width;
  int height;
};

inline bool appendUnique(Size out[], const int maxOut, int& count, const Size size) {
  if (size.height <= 0 || count >= maxOut) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (out[i].width == size.width && out[i].height == size.height) {
      return true;
    }
  }
  out[count++] = size;
  return true;
}

inline int all(Size out[], const int maxOut) {
  if (out == nullptr || maxOut <= 0) {
    return 0;
  }

  int count = 0;
  appendUnique(out, maxOut, count, Size{0, ForkDriftMetrics::values.homeCoverHeight});
  appendUnique(out, maxOut, count, Size{LyraCarouselTheme::kCenterCoverW, LyraCarouselTheme::kCenterCoverH});
  appendUnique(out, maxOut, count, Size{LyraCarouselTheme::kSideCoverW, LyraCarouselTheme::kSideCoverH});
#if ENABLE_POKEMON_PARTY
  appendUnique(out, maxOut, count, Size{PokemonPartyTheme::kCoverIconSize, PokemonPartyTheme::kCoverIconSize});
#endif
  return count;
}

}  // namespace coverthumbs

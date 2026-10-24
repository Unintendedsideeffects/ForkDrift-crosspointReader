#pragma once

#include <cmath>
#include <cstdint>

#include "../../../src/fontIds.h"
#include "FeatureFlags.h"

inline int fontIdForScale(const int baseId, const float scale) {
  if (!(scale > 0.0f)) return baseId;

  struct Family {
    const int* ids;
    const uint8_t* points;
    uint8_t count;
  };

  static constexpr uint8_t kPts81012[] = {8, 10, 12};
  static constexpr int kUi[] = {UI_8_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID};

#if ENABLE_BOOKERLY_FONTS || ENABLE_NOTOSANS_FONTS || ENABLE_LEXENDDECA_FONTS || ENABLE_BITTER_FONTS || \
    ENABLE_CHAREINK_FONTS
  static constexpr uint8_t kPts12141618[] = {12, 14, 16, 18};
#endif
#if ENABLE_OPENDYSLEXIC_FONTS
  static constexpr uint8_t kPts8101214[] = {8, 10, 12, 14};
#endif

#if ENABLE_BOOKERLY_FONTS
  static constexpr int kNotoSerif[] = {NOTOSERIF_12_FONT_ID, NOTOSERIF_14_FONT_ID, NOTOSERIF_16_FONT_ID,
                                       NOTOSERIF_18_FONT_ID};
#endif
#if ENABLE_NOTOSANS_FONTS
  static constexpr int kNotoSans[] = {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID,
                                      NOTOSANS_18_FONT_ID};
#endif
#if ENABLE_OPENDYSLEXIC_FONTS
  static constexpr int kOpenDyslexic[] = {OPENDYSLEXIC_8_FONT_ID, OPENDYSLEXIC_10_FONT_ID, OPENDYSLEXIC_12_FONT_ID,
                                          OPENDYSLEXIC_14_FONT_ID};
#endif
#if ENABLE_LEXENDDECA_FONTS
  static constexpr int kLexendDeca[] = {LEXENDDECA_12_FONT_ID, LEXENDDECA_14_FONT_ID, LEXENDDECA_16_FONT_ID,
                                        LEXENDDECA_18_FONT_ID};
#endif
#if ENABLE_BITTER_FONTS
  static constexpr int kBitter[] = {BITTER_12_FONT_ID, BITTER_14_FONT_ID, BITTER_16_FONT_ID, BITTER_18_FONT_ID};
#endif
#if ENABLE_CHAREINK_FONTS
  static constexpr int kChareInk[] = {CHAREINK_12_FONT_ID, CHAREINK_14_FONT_ID, CHAREINK_16_FONT_ID,
                                      CHAREINK_18_FONT_ID};
#endif

  static constexpr Family kFamilies[] = {
      {kUi, kPts81012, 3},
#if ENABLE_BOOKERLY_FONTS
      {kNotoSerif, kPts12141618, 4},
#endif
#if ENABLE_NOTOSANS_FONTS
      {kNotoSans, kPts12141618, 4},
#endif
#if ENABLE_OPENDYSLEXIC_FONTS
      {kOpenDyslexic, kPts8101214, 4},
#endif
#if ENABLE_LEXENDDECA_FONTS
      {kLexendDeca, kPts12141618, 4},
#endif
#if ENABLE_BITTER_FONTS
      {kBitter, kPts12141618, 4},
#endif
#if ENABLE_CHAREINK_FONTS
      {kChareInk, kPts12141618, 4},
#endif
  };

  for (const Family& family : kFamilies) {
    int baseIndex = -1;
    for (uint8_t i = 0; i < family.count; ++i) {
      if (family.ids[i] == baseId) {
        baseIndex = i;
        break;
      }
    }
    if (baseIndex < 0) continue;

    const float target = static_cast<float>(family.points[baseIndex]) * scale;
    uint8_t pick = static_cast<uint8_t>(baseIndex);
    float bestDist = 1.0e9f;
    const uint8_t begin = scale >= 1.0f ? static_cast<uint8_t>(baseIndex) : 0;
    const uint8_t end = scale >= 1.0f ? family.count : static_cast<uint8_t>(baseIndex + 1);
    for (uint8_t i = begin; i < end; ++i) {
      const float dist = std::fabs(static_cast<float>(family.points[i]) - target);
      if (dist < bestDist) {
        bestDist = dist;
        pick = i;
      }
    }
    return family.ids[pick];
  }
  return baseId;
}

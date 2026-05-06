#pragma once

#include "CrossPointSettings.h"

namespace features::status_overlay {

constexpr int kStatusIconSize = 16;
constexpr int kStatusIconGap = 4;
constexpr int kStatusBarPadTop = 3;
constexpr int kStatusBarPadBottom = 4;
constexpr int kStatusBarPadH = 6;
constexpr int kStatusBarHeight = kStatusIconSize + kStatusBarPadTop + kStatusBarPadBottom;

inline bool isEnabled() { return SETTINGS.isGlobalStatusBarEnabled(); }

inline bool preventsAutoSleep() { return SETTINGS.globalStatusBarPreventsAutoSleep(); }

inline int topInset() {
  return isEnabled() && SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_TOP ? kStatusBarHeight : 0;
}

inline int bottomInset() {
  return isEnabled() && SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM ? kStatusBarHeight
                                                                                                  : 0;
}

}  // namespace features::status_overlay

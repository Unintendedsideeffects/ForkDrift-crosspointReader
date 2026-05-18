#pragma once

#include <FeatureFlags.h>

#include "CrossPointSettings.h"

namespace features::status_overlay {

// Intrinsic glyph geometry of the status icons themselves (not bar padding).
constexpr int kStatusIconSize = 16;
constexpr int kStatusIconGap = 4;

// The global status bar is the same entity as the reader's status bar — it is
// the superset rendered everywhere, not a second bar with its own geometry.
// Size and padding therefore follow the reader bar's (more polished) metrics,
// which are the single source of truth (ThemeMetrics, consumed by
// BaseTheme::drawStatusBar / UITheme::getStatusBarHeight). Defined out-of-line
// in Registration.cpp to keep this widely-included header free of the theme
// system include chain.
int barHeight();
int padH();
int padTop();  // top offset that vertically centres the icon row in the band

inline bool isEnabled() { return ENABLE_GLOBAL_STATUS_BAR && SETTINGS.isGlobalStatusBarEnabled(); }

inline bool preventsAutoSleep() { return SETTINGS.globalStatusBarPreventsAutoSleep(); }

int topInset();
int bottomInset();

}  // namespace features::status_overlay

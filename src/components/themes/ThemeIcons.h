#pragma once
#include <cstdint>

#include "components/themes/BaseTheme.h"

// Icon lookup is per theme AND per size, and no theme's table covers every
// UIIcon. Before this helper existed, a miss returned nullptr and every call
// site simply skipped drawing — an unmapped icon was an invisible gap on the
// menu row, with no compile error and nothing in the log. It cost two
// debugging rounds during the Extras submenu work; see the comments in
// src/activities/home/HomeExtras.cpp and the 2026-08-16 entry in
// docs/FINDINGS.md.
//
// A theme's own table is consulted first (so a theme can restyle an icon). On a
// miss, resolve() tries the other menu icon size, then the shared catalogue
// below, and only logs when the icon has no art at either size anywhere. That
// makes "theme forgot to wire an icon" impossible for any icon that has art:
// test_theme_icons.cpp enumerates every UIIcon to keep it that way.
namespace theme_icons {

struct ResolvedIcon {
  const uint8_t* bmp = nullptr;
  int size = 0;  // the size to draw at; may differ from the requested size
};

// `lookup` is the theme's own file-local table function.
// Returns {nullptr, 0} when neither size resolves, having logged a warning the
// first time that (icon, size) pair misses.
ResolvedIcon resolve(const uint8_t* (*lookup)(UIIcon, int), UIIcon icon, int preferredSize, int fallbackSize);

// Every icon bitmap the firmware ships, at its native size(s). Returns nullptr
// when `icon` has no art at `size` (e.g. Text exists only at 24px).
const uint8_t* standard(UIIcon icon, int size);

// For themes whose lookup has no size axis: log a miss once per icon.
void warnMissingIcon(UIIcon icon, int size);

}  // namespace theme_icons

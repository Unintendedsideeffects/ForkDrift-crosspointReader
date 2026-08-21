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
// This does NOT change any theme's table. It only decides what happens on a
// miss: try the other menu icon size, and if that misses too, say so once.
namespace theme_icons {

struct ResolvedIcon {
  const uint8_t* bmp = nullptr;
  int size = 0;  // the size to draw at; may differ from the requested size
};

// `lookup` is the theme's own file-local table function.
// Returns {nullptr, 0} when neither size resolves, having logged a warning the
// first time that (icon, size) pair misses.
ResolvedIcon resolve(const uint8_t* (*lookup)(UIIcon, int), UIIcon icon, int preferredSize, int fallbackSize);

// For themes whose lookup has no size axis: log a miss once per icon.
void warnMissingIcon(UIIcon icon, int size);

}  // namespace theme_icons

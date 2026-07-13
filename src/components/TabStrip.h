#pragma once

#include <cstddef>
#include <vector>

class GfxRenderer;
struct Rect;

struct TabStripItem {
  const char* label;
  bool enabled;
};

// Draws a centered, evenly-spaced tab strip. Disabled items remain in the
// model but are hidden in this phase, preserving one index space for model,
// navigation, activation, and rendering.
void drawTabStrip(GfxRenderer& renderer, Rect rect, const std::vector<TabStripItem>& tabs, size_t selectedIndex);

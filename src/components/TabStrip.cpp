#include "TabStrip.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "components/themes/BaseTheme.h"
#include "fontIds.h"

void drawTabStrip(GfxRenderer& renderer, const Rect rect, const std::vector<TabStripItem>& tabs,
                  const size_t selectedIndex) {
  const size_t visibleCount =
      std::count_if(tabs.begin(), tabs.end(), [](const TabStripItem& tab) { return tab.enabled; });
  if (visibleCount == 0 || rect.width <= 0 || rect.height <= 0) {
    return;
  }

  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  const int tileWidth = rect.width / static_cast<int>(visibleCount);
  const int labelHeight = renderer.getLineHeight(UI_10_FONT_ID);

  size_t visibleIndex = 0;
  for (size_t modelIndex = 0; modelIndex < tabs.size(); ++modelIndex) {
    if (!tabs[modelIndex].enabled) {
      continue;
    }

    const int tileX = rect.x + static_cast<int>(visibleIndex) * tileWidth;
    const int width = visibleIndex + 1 == visibleCount ? rect.x + rect.width - tileX : tileWidth;
    if (modelIndex == selectedIndex) {
      renderer.fillRoundedRect(tileX + 3, rect.y + 3, std::max(0, width - 6), std::max(0, rect.height - 6), 4,
                               Color::LightGray);
    }

    const std::string label = renderer.truncatedText(UI_10_FONT_ID, tabs[modelIndex].label, std::max(0, width - 8));
    const int labelWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    const int labelY = rect.y + (rect.height - labelHeight) / 2;
    renderer.drawText(UI_10_FONT_ID, tileX + (width - labelWidth) / 2, labelY, label.c_str(), true);
    ++visibleIndex;
  }
}

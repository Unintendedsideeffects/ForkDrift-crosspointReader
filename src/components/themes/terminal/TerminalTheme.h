#pragma once

#include "components/themes/BaseTheme.h"

class GfxRenderer;

// Terminal theme metrics.
//
// Built by copying BaseMetrics (Classic) and tweaking only the chrome bands so
// the rest of the system (keyboard, popups, covers — inherited from BaseTheme)
// keeps working unchanged. constexpr build => zero runtime cost, lives in flash.
namespace TerminalMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics m = BaseMetrics::values;
  m.topPadding = 6;
  m.headerHeight = 44;    // single inverted command-bar line
  m.tabBarHeight = 44;    // bracketed category chips
  m.verticalSpacing = 8;  // tight, dense terminal rows
  m.contentSidePadding = 18;
  m.listRowHeight = 32;
  m.buttonHintsHeight = 40;  // bracketed function-key footer
  return m;
}
constexpr ThemeMetrics values = makeValues();
}  // namespace TerminalMetrics

// Retro-futuristic TTY theme. High-contrast 1-bit chrome: an inverted command
// bar, bracketed tab chips, a `>` selection cursor on a full-bleed inverted row,
// `#`-prefixed section dividers, and a bracketed function-key footer. Only the
// settings-screen primitives are overridden; everything else falls back to the
// Classic BaseTheme implementation.
class TerminalTheme : public BaseTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                const std::function<bool(int index)>& isHeader = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                       bool allowInvertedText = false) const override;
};

#include "TerminalTheme.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <string>

#include "CrossPointSettings.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"

namespace {
// Terminal chrome leans on ALL-CAPS labels (TTY convention). Done on a local
// std::string copy so the caller's string is untouched; only used on short,
// non-hot-path UI labels (titles, tabs, headers).
std::string toUpperCopy(const char* s) {
  std::string out(s ? s : "");
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return out;
}
}  // namespace

void TerminalTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle) const {
  const int pad = TerminalMetrics::values.contentSidePadding;

  // Battery, right-aligned (suppressed when the global status overlay owns it).
  const bool showHeaderBattery = !features::status_overlay::isEnabled();
  const int batteryX = rect.x + rect.width - 12 - TerminalMetrics::values.batteryWidth;
  if (showHeaderBattery) {
    constexpr int maxBatteryWidth = 80;
    renderer.fillRect(rect.x + rect.width - maxBatteryWidth, rect.y + 5, maxBatteryWidth,
                      TerminalMetrics::values.batteryHeight + 10, false);
    const bool showBatteryPercentage =
        SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
    drawBatteryRight(
        renderer,
        Rect{batteryX, rect.y + 5, TerminalMetrics::values.batteryWidth, TerminalMetrics::values.batteryHeight},
        showBatteryPercentage);
  }

  // Title rendered as an inverted "chip" — a solid black tag with white text,
  // the signature terminal menu-bar label. (White-on-black keeps the battery,
  // drawn in black, fully visible on the surrounding white background.)
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int chipH = lineHeight + 6;
  const int chipY = rect.y + (rect.height - 4 - chipH) / 2;
  int chipRightX = rect.x + pad;
  if (title) {
    const std::string label = toUpperCopy(title);
    constexpr int chipPadX = 8;
    const int textW = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::BOLD);
    const int chipW = textW + chipPadX * 2;
    renderer.fillRect(rect.x + pad, chipY, chipW, chipH, true);
    renderer.drawText(UI_12_FONT_ID, rect.x + pad + chipPadX, chipY + 3, label.c_str(), false, EpdFontFamily::BOLD);
    chipRightX = rect.x + pad + chipW;
  }

  // Version string inline, right-aligned just left of the battery cluster.
  if (subtitle) {
    const int batteryReserve = showHeaderBattery ? 90 : pad;
    const int maxW = rect.x + rect.width - batteryReserve - chipRightX - 8;
    if (maxW > 0) {
      auto version = renderer.truncatedText(SMALL_FONT_ID, subtitle, maxW, EpdFontFamily::REGULAR);
      const int versionW = renderer.getTextWidth(SMALL_FONT_ID, version.c_str());
      renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - batteryReserve - versionW,
                        chipY + (chipH - renderer.getLineHeight(SMALL_FONT_ID)) / 2, version.c_str(), true);
    }
  }

  // Double rule under the bar: a thick separator plus a thin "scanline" accent.
  const int ruleY = rect.y + rect.height - 3;
  renderer.fillRect(rect.x, ruleY, rect.width, 2, true);
  renderer.drawLine(rect.x, ruleY - 3, rect.x + rect.width - 1, ruleY - 3, true);
}

void TerminalTheme::drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                                  const char* rightLabel) const {
  const int pad = TerminalMetrics::values.contentSidePadding;
  int rightReserve = pad;
  if (rightLabel) {
    auto right = renderer.truncatedText(SMALL_FONT_ID, rightLabel, 200, EpdFontFamily::REGULAR);
    const int rightW = renderer.getTextWidth(SMALL_FONT_ID, right.c_str());
    renderer.drawText(SMALL_FONT_ID, rect.x + rect.width - pad - rightW, rect.y + 7, right.c_str(), true);
    rightReserve += rightW + 10;
  }

  // Prompt-style header: "> LABEL".
  const std::string prompt = "> " + toUpperCopy(label);
  auto truncated =
      renderer.truncatedText(UI_12_FONT_ID, prompt.c_str(), rect.width - pad - rightReserve, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, rect.x + pad, rect.y, truncated.c_str(), true, EpdFontFamily::BOLD);
}

void TerminalTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                               const bool selected) const {
  if (tabs.empty()) {
    return;
  }

  const int pad = TerminalMetrics::values.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int textY = rect.y + (rect.height - lineHeight) / 2;
  constexpr int chipPadX = 6;

  int x = rect.x + pad;
  for (const auto& tab : tabs) {
    const std::string label = toUpperCopy(tab.label);
    const auto style = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, label.c_str(), style);

    if (tab.selected) {
      // Active category = inverted chip (white text on a black block).
      renderer.fillRect(x - chipPadX, textY - 3, textW + chipPadX * 2, lineHeight + 6, true);
      renderer.drawText(UI_10_FONT_ID, x, textY, label.c_str(), false, style);
      x += textW + chipPadX * 2 + TerminalMetrics::values.tabSpacing;
    } else {
      renderer.drawText(UI_10_FONT_ID, x, textY, label.c_str(), true, style);
      x += textW + TerminalMetrics::values.tabSpacing;
    }
  }

  // Baseline rule: thick (3px) when the tab row itself is focused (tabs can be
  // switched), thin (1px) otherwise.
  const int ruleY = rect.y + rect.height - 1;
  if (selected) {
    renderer.fillRect(rect.x, ruleY - 2, rect.width, 3, true);
  } else {
    renderer.drawLine(rect.x, ruleY, rect.x + rect.width - 1, ruleY, true);
  }
}

void TerminalTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                             const std::function<std::string(int index)>& rowTitle,
                             const std::function<std::string(int index)>& rowSubtitle,
                             const std::function<UIIcon(int index)>& rowIcon,
                             const std::function<std::string(int index)>& rowValue, bool highlightValue,
                             const std::function<bool(int index)>& rowDimmed,
                             const std::function<bool(int index)>& isHeader) const {
  (void)rowIcon;
  (void)highlightValue;

  const int pad = TerminalMetrics::values.contentSidePadding;
  const int rowHeight = TerminalMetrics::values.listRowHeight;
  const int pageItems = std::max(1, rect.height / rowHeight);
  constexpr int sectionHeaderTopPadding = 18;
  const int contentWidth = rect.width - 5;

  // Right-side page arrows (terminal-plain triangles) when paginated.
  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int arrowSize = 6;
    constexpr int margin = 12;
    const int centerX = rect.x + rect.width - margin;
    const int top = rect.y;
    const int bottom = rect.y + rect.height - arrowSize;
    for (int i = 0; i < arrowSize; ++i) {
      const int w = 1 + i * 2;
      renderer.drawLine(centerX - i, top + i, centerX - i + w - 1, top + i, true);
    }
    for (int i = 0; i < arrowSize; ++i) {
      const int w = 1 + (arrowSize - 1 - i) * 2;
      const int sx = centerX - (arrowSize - 1 - i);
      renderer.drawLine(sx, bottom - arrowSize + 1 + i, sx + w - 1, bottom - arrowSize + 1 + i, true);
    }
  }

  const int pageStartIndex = (selectedIndex < 0) ? 0 : (selectedIndex / pageItems) * pageItems;

  // Full-bleed inverted selection bar (the TTY cursor line).
  if (selectedIndex >= 0 && !(isHeader && isHeader(selectedIndex))) {
    int selY = rect.y;
    for (int j = pageStartIndex; j < selectedIndex; j++) {
      selY += rowHeight;
      if (isHeader && isHeader(j + 1)) selY += sectionHeaderTopPadding;
    }
    renderer.fillRect(rect.x, selY - 2, rect.width, rowHeight, true);
  }

  const std::string cursor = ">";
  const int cursorW = renderer.getTextWidth(UI_10_FONT_ID, "> ");
  const int lineH = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int minValueGap = 10;

  int currentY = rect.y;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    if (i > pageStartIndex && isHeader && isHeader(i)) currentY += sectionHeaderTopPadding;
    const int itemY = currentY;
    currentY += rowHeight;
    const bool isSel = (i == selectedIndex);

    // Section header: "# LABEL" comment-style divider with an underline rule.
    if (isHeader && isHeader(i)) {
      const std::string label = "# " + toUpperCopy(rowTitle(i).c_str());
      auto truncated =
          renderer.truncatedText(UI_10_FONT_ID, label.c_str(), contentWidth - pad * 2, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, rect.x + pad, itemY + 5, truncated.c_str(), true, EpdFontFamily::BOLD);
      renderer.drawLine(rect.x + pad, itemY + rowHeight - 1, rect.x + contentWidth - pad, itemY + rowHeight - 1, true);
      continue;
    }

    // Selection cursor in the left gutter.
    if (isSel) {
      renderer.drawText(UI_10_FONT_ID, rect.x + pad, itemY, cursor.c_str(), false);
    }
    const int labelX = rect.x + pad + cursorW;
    int rowTextWidth = contentWidth - pad - cursorW - pad;

    // Value field, right-aligned and bracketed: [On], [Noto Sans], ...
    std::string valueText;
    if (rowValue != nullptr) {
      const std::string raw = rowValue(i);
      if (!raw.empty()) {
        const int maxValW = std::max(0, rowTextWidth - 40 - minValueGap);
        valueText = "[" + std::string(renderer.truncatedText(UI_10_FONT_ID, raw.c_str(), maxValW)) + "]";
        const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str()) + minValueGap;
        rowTextWidth -= valueWidth;
      }
    }

    auto title = renderer.truncatedText(UI_10_FONT_ID, rowTitle(i).c_str(), std::max(1, rowTextWidth));
    renderer.drawText(UI_10_FONT_ID, labelX, itemY, title.c_str(), !isSel);

    // Dimmed (unavailable) rows: checkerboard dither over the label for a
    // "ghosted" gray effect that survives 1-bit e-ink.
    if (rowDimmed && rowDimmed(i) && !isSel) {
      const int titleWidth = renderer.getTextWidth(UI_10_FONT_ID, title.c_str());
      for (int py = itemY; py < itemY + lineH; py++)
        for (int px = labelX; px < labelX + titleWidth; px++)
          if ((px + py) % 2 == 0) renderer.drawPixel(px, py, false);
    }

    if (rowSubtitle != nullptr) {
      const std::string sub = rowSubtitle(i);
      if (!sub.empty()) {
        auto subtitle = renderer.truncatedText(SMALL_FONT_ID, sub.c_str(), rowTextWidth);
        renderer.drawText(SMALL_FONT_ID, labelX, itemY + 22, subtitle.c_str(), !isSel);
      }
    }

    if (!valueText.empty()) {
      const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
      renderer.drawText(UI_10_FONT_ID, rect.x + contentWidth - pad - valueWidth, itemY, valueText.c_str(), !isSel);
    }
  }
}

void TerminalTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                    const char* btn4, const bool allowInvertedText) const {
  const GfxRenderer::Orientation origOrientation = renderer.getOrientation();
  const bool invertText = allowInvertedText && origOrientation == GfxRenderer::Orientation::PortraitInverted;
  renderer.setOrientation(invertText ? GfxRenderer::Orientation::PortraitInverted : GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int buttonHeight = TerminalMetrics::values.buttonHintsHeight;
  const int buttonY = invertText ? pageHeight : buttonHeight;
  constexpr int buttonWidth = 106;
  constexpr int textYOffset = 9;
  constexpr int x4ButtonPositions[] = {25, 130, 245, 350};
  constexpr int x3ButtonPositions[] = {38, 154, 268, 384};
  const int* buttonPositions = screenWidth > 500 ? x3ButtonPositions : x4ButtonPositions;
  const char* labels[] = {btn1, btn2, btn3, btn4};

  // Single full-width divider rule along the top of the footer band — the TTY
  // status-line separator. Works in both orientations because the band top is
  // always at (pageHeight - buttonY).
  const int bandTop = pageHeight - buttonY;
  renderer.drawLine(0, bandTop, screenWidth - 1, bandTop, true);

  // Each hint as a bracketed key label: [BACK] [SEL] [UP] [DN].
  for (int i = 0; i < 4; i++) {
    if (labels[i] == nullptr || labels[i][0] == '\0') {
      continue;
    }
    const int x = buttonPositions[invertText ? 3 - i : i];
    const std::string label = "[" + toUpperCopy(labels[i]) + "]";
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label.c_str());
    const int textX = x + (buttonWidth - 1 - textWidth) / 2;
    renderer.drawText(UI_10_FONT_ID, textX, bandTop + textYOffset, label.c_str(), true);
  }

  renderer.setOrientation(origOrientation);
}

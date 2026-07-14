#include "TocActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "MappedInputManager.h"
#include "activities/TaskShutdown.h"
#include "components/ScreenComponents.h"
#include "fontIds.h"

namespace {
constexpr int TITLE_Y = 15;
constexpr int CONTENT_START_Y = 60;
constexpr int LINE_HEIGHT = 30;
constexpr int LEFT_MARGIN = 20;
constexpr int RIGHT_MARGIN = 40;
constexpr int INDENT_STEP = 15;
constexpr int BOTTOM_MARGIN = 60;
constexpr int SKIP_PAGE_MS = 700;

int wrapIndex(int index, int total) {
  if (total <= 0) {
    return 0;
  }
  index %= total;
  if (index < 0) {
    index += total;
  }
  return index;
}
}  // namespace

int TocActivity::getVisibleItems() const {
  const int screenHeight = renderer.getScreenHeight();
  const int availableHeight = screenHeight - CONTENT_START_Y - BOTTOM_MARGIN;
  int items = availableHeight / LINE_HEIGHT;
  if (items < 1) {
    items = 1;
  }
  return items;
}

void TocActivity::ensureSelectionVisible(int visibleItems) {
  if (selectedIndex < scrollOffset) {
    scrollOffset = selectedIndex;
  } else if (selectedIndex >= scrollOffset + visibleItems) {
    scrollOffset = selectedIndex - visibleItems + 1;
  }
}

void TocActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  scrollOffset = 0;
  requestUpdate();
}

void TocActivity::onExit() { Activity::onExit(); }

void TocActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoBack();
    return;
  }

  const int totalItems = static_cast<int>(tocEntries.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (totalItems > 0 && selectedIndex >= 0 && selectedIndex < totalItems) {
      onSelect(static_cast<size_t>(selectedIndex));
    }
    return;
  }

  if (totalItems == 0) {
    return;
  }

  const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                            mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (!prevReleased && !nextReleased) {
    return;
  }

  const int visibleItems = getVisibleItems();
  const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;

  if (prevReleased) {
    selectedIndex =
        skipPage ? wrapIndex(selectedIndex - visibleItems, totalItems) : wrapIndex(selectedIndex - 1, totalItems);
  } else if (nextReleased) {
    selectedIndex =
        skipPage ? wrapIndex(selectedIndex + visibleItems, totalItems) : wrapIndex(selectedIndex + 1, totalItems);
  }

  ensureSelectionVisible(visibleItems);
  requestUpdate();
}

void TocActivity::render(Activity::RenderLock&& lock) { renderScreen(); }

void TocActivity::renderScreen() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, TITLE_Y, "Table of Contents", true, EpdFontFamily::BOLD);

  if (tocEntries.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, CONTENT_START_Y, "No headings");
    const auto labels = mappedInput.mapLabels("« Back", "", "", "");
    renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int visibleItems = getVisibleItems();
  const int totalItems = static_cast<int>(tocEntries.size());

  const int selectedRow = selectedIndex - scrollOffset;
  if (selectedRow >= 0 && selectedRow < visibleItems) {
    renderer.fillRect(0, CONTENT_START_Y + selectedRow * LINE_HEIGHT - 2, pageWidth - RIGHT_MARGIN, LINE_HEIGHT);
  }

  for (int i = 0; i < visibleItems; i++) {
    const int itemIndex = scrollOffset + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const auto& entry = tocEntries[itemIndex];
    const int level = std::max(1, static_cast<int>(entry.level));
    const int indent = LEFT_MARGIN + (level - 1) * INDENT_STEP;
    const int textY = CONTENT_START_Y + i * LINE_HEIGHT;
    const bool isSelected = (itemIndex == selectedIndex);

    const std::string title = entry.title.empty() ? "Untitled" : entry.title;
    const int maxWidth = pageWidth - indent - RIGHT_MARGIN;
    const std::string truncated = maxWidth > 0 ? renderer.truncatedText(UI_10_FONT_ID, title.c_str(), maxWidth) : "";

    renderer.drawText(UI_10_FONT_ID, indent, textY, truncated.c_str(), !isSelected);
  }

  const int contentHeight = pageHeight - CONTENT_START_Y - BOTTOM_MARGIN;
  const int totalPages = (totalItems + visibleItems - 1) / visibleItems;
  const int currentPage = scrollOffset / visibleItems + 1;
  ScreenComponents::drawScrollIndicator(renderer, currentPage, totalPages, CONTENT_START_Y, contentHeight);

  const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#include "EpubReaderHighlightListActivity.h"

#if ENABLE_ANNOTATIONS

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

static constexpr int ROW_HEIGHT = 50;
static constexpr int LIST_START_Y = 60;
static constexpr unsigned long HIGHLIGHT_DELETE_HOLD_MS = 1000;

int EpubReaderHighlightListActivity::getPageItems() const {
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = LIST_START_Y + hintGutterHeight;
  const int available = renderer.getScreenHeight() - startY - ROW_HEIGHT;
  return std::max(1, available / ROW_HEIGHT);
}

void EpubReaderHighlightListActivity::onEnter() {
  Activity::onEnter();
  annotations = ANNOTATIONS.all();
  selectedIndex = 0;
  requestUpdate();
}

void EpubReaderHighlightListActivity::onExit() { Activity::onExit(); }

void EpubReaderHighlightListActivity::deleteSelectedAnnotation() {
  if (annotations.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(annotations.size())) return;

  const Annotation selectedAnnotation = annotations[selectedIndex];
  if (ANNOTATIONS.removeAt(selectedAnnotation.spineIndex, selectedAnnotation.page, selectedAnnotation.startWord) == 0) {
    return;
  }

  annotations = ANNOTATIONS.all();
  if (annotations.empty()) {
    selectedIndex = 0;
  } else if (selectedIndex >= static_cast<int>(annotations.size())) {
    selectedIndex = static_cast<int>(annotations.size()) - 1;
  }
  requestUpdate();
}

void EpubReaderHighlightListActivity::showHighlightActionMenu(bool ignoreInitialConfirmRelease) {
  if (annotations.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(annotations.size())) return;

  const Annotation selectedAnnotation = annotations[selectedIndex];
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(1);
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, selectedAnnotation.text, std::move(items),
                                                  ignoreInitialConfirmRelease),
      [this, selectedAnnotation](const ActivityResult& result) {
        longPressConfirmHandled = false;
        if (result.isCancelled) {
          requestUpdate();
          return;
        }

        const auto* actionResult = std::get_if<FileBrowserActionResult>(&result.data);
        if (!actionResult || static_cast<FileBrowserAction>(actionResult->action) != FileBrowserAction::Delete) {
          requestUpdate();
          return;
        }

        const auto it =
            std::find_if(annotations.begin(), annotations.end(), [&selectedAnnotation](const Annotation& a) {
              return a.spineIndex == selectedAnnotation.spineIndex && a.page == selectedAnnotation.page &&
                     a.startWord == selectedAnnotation.startWord && a.endWord == selectedAnnotation.endWord &&
                     a.text == selectedAnnotation.text;
            });
        if (it != annotations.end()) {
          selectedIndex = static_cast<int>(std::distance(annotations.begin(), it));
          deleteSelectedAnnotation();
        } else {
          requestUpdate();
        }
      });
}

void EpubReaderHighlightListActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  if (!annotations.empty() && !longPressConfirmHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= HIGHLIGHT_DELETE_HOLD_MS) {
    longPressConfirmHandled = true;
    showHighlightActionMenu(true);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (longPressConfirmHandled) {
      longPressConfirmHandled = false;
      return;
    }
    if (!annotations.empty() && selectedIndex >= 0 && selectedIndex < static_cast<int>(annotations.size())) {
      const auto& annotation = annotations[selectedIndex];
      setResult(ProgressChangeResult{annotation.spineIndex, annotation.page});
      finish();
    }
    return;
  }

  const int total = static_cast<int>(annotations.size());
  if (total == 0) return;

  const int pageItems = getPageItems();

  buttonNavigator.onNextRelease([this, total] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, total] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, total, pageItems] {
    selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, total, pageItems);
    requestUpdate();
  });
}

void EpubReaderHighlightListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  if (annotations.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, LIST_START_Y + contentY + 20, tr(STR_NO_HIGHLIGHTS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }

  const int pageItems = getPageItems();
  const int total = static_cast<int>(annotations.size());
  const int pageStartIndex = (selectedIndex / pageItems) * pageItems;
  const int marginLeft = contentX + 20;
  const int textWidth = std::max(1, contentWidth - 40);

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= total) break;

    const int rowY = LIST_START_Y + contentY + i * ROW_HEIGHT;
    const bool isSelected = (itemIndex == selectedIndex);

    if (isSelected) {
      renderer.fillRect(contentX, rowY, contentWidth - 1, ROW_HEIGHT, true);
    }

    const auto lines = renderer.wrappedText(UI_10_FONT_ID, annotations[itemIndex].text.c_str(), textWidth, 2);
    for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
      renderer.drawText(UI_10_FONT_ID, marginLeft, rowY + 6 + static_cast<int>(lineIndex) * 20,
                        lines[lineIndex].c_str(), !isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

#endif  // ENABLE_ANNOTATIONS

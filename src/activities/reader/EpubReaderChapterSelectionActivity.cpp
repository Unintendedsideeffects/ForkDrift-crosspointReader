#include "EpubReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"

bool EpubReaderChapterSelectionActivity::hasSyncOption() const {
  return core::FeatureModules::hasKoreaderSyncCredentials();
}

int EpubReaderChapterSelectionActivity::getTotalItems() const {
  const int syncCount = hasSyncOption() ? 2 : 0;
  return epub->getTocItemsCount() + syncCount;
}

bool EpubReaderChapterSelectionActivity::isSyncItem(int index) const {
  if (!hasSyncOption()) {
    return false;
  }
  return index == 0 || index == getTotalItems() - 1;
}

int EpubReaderChapterSelectionActivity::tocIndexFromItemIndex(int itemIndex) const {
  const int offset = hasSyncOption() ? 1 : 0;
  return itemIndex - offset;
}

int EpubReaderChapterSelectionActivity::getPageItems() const {
  constexpr int lineHeight = 30;

  const int screenHeight = renderer.getScreenHeight();
  const auto orientation = renderer.getOrientation();
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int startY = 60 + hintGutterHeight;
  const int availableHeight = screenHeight - startY - lineHeight;
  return std::max(1, availableHeight / lineHeight);
}

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  const int syncOffset = hasSyncOption() ? 1 : 0;
  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }
  selectorIndex += syncOffset;

  requestUpdate();
}

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::onSyncPosition(int newSpineIndex, int newPage) {
  (void)newPage;
  setResult(ChapterResult{newSpineIndex});
  finish();
}

void EpubReaderChapterSelectionActivity::launchSyncActivity() {
  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPagesInSpine};
  KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";

  startActivityForResult(
      std::make_unique<KOReaderSyncActivity>(renderer, mappedInput, epubPath, currentSpineIndex, currentPage,
                                             totalPagesInSpine, std::move(localKoPos), std::move(localChapterName)),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          const auto& sync = std::get<SyncResult>(result.data);
          onSyncPosition(sync.spineIndex, sync.page);
        } else {
          requestUpdate();
        }
      });
}

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (isSyncItem(selectorIndex)) {
      launchSyncActivity();
      return;
    }

    const int tocIndex = tocIndexFromItemIndex(selectorIndex);
    const auto newSpineIndex = epub->getSpineIndexForTocIndex(tocIndex);
    if (newSpineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{newSpineIndex});
      finish();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  }

  buttonNavigator.onNextRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
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
  const int pageItems = getPageItems();
  const int totalItems = getTotalItems();

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_SELECT_CHAPTER), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_SELECT_CHAPTER), true, EpdFontFamily::BOLD);

  const int pageStartIndex = selectorIndex / pageItems * pageItems;
  renderer.fillRect(contentX, 60 + contentY + (selectorIndex % pageItems) * 30 - 2, contentWidth - 1, 30);

  for (int i = 0; i < pageItems; i++) {
    const int itemIndex = pageStartIndex + i;
    if (itemIndex >= totalItems) {
      break;
    }

    const int displayY = 60 + contentY + i * 30;
    const bool isSelected = itemIndex == selectorIndex;

    if (isSyncItem(itemIndex)) {
      renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, tr(STR_SYNC_PROGRESS), !isSelected);
      continue;
    }

    const int tocIndex = tocIndexFromItemIndex(itemIndex);
    const auto item = epub->getTocItem(tocIndex);
    const int indentSize = contentX + 20 + (item.level - 1) * 15;
    const int chapterWidth = std::max(10, contentX + contentWidth - 20 - indentSize);
    const std::string chapterName = renderer.truncatedText(UI_10_FONT_ID, item.title.c_str(), chapterWidth);

    renderer.drawText(UI_10_FONT_ID, indentSize, displayY, chapterName.c_str(), !isSelected);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

#include "EpubReaderMenuActivity.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <HeapGuard.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "ReaderOptionsMemoryPolicy.h"
#include "components/UITheme.h"
#include "core/features/FeatureCatalog.h"
#include "fontIds.h"

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool isBookCompleted,
                                               ReaderPreviewRefreshFn previewRefresh
#if ENABLE_BOOKMARKS
                                               ,
                                               const bool hasBookmarks, const bool isCurrentPageBookmarked
#endif
                                               )
    : Activity("EpubReaderMenu", renderer, mappedInput),
      menuItems(buildMenuItems(hasFootnotes, isBookCompleted
#if ENABLE_BOOKMARKS
                               ,
                               hasBookmarks, isCurrentPageBookmarked
#endif
                               )),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent),
      previewRefresh_(std::move(previewRefresh)) {
}

std::vector<EpubReaderMenuActivity::MenuItem> EpubReaderMenuActivity::buildMenuItems(bool hasFootnotes,
                                                                                     bool isBookCompleted
#if ENABLE_BOOKMARKS
                                                                                     ,
                                                                                     bool hasBookmarks,
                                                                                     bool isCurrentPageBookmarked
#endif
) {
  std::vector<MenuItem> items;
  items.reserve(15);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
#if ENABLE_TEXT_SELECTION
  items.push_back({MenuAction::SELECT_TEXT, StrId::STR_SELECT_TEXT});
#endif
  items.push_back({MenuAction::READER_OPTIONS, StrId::STR_CAT_READER});
  items.push_back({MenuAction::CONTROLS_OPTIONS, StrId::STR_CAT_CONTROLS});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  items.push_back(
      {MenuAction::TOGGLE_COMPLETED, isBookCompleted ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
#if ENABLE_TEXT_SELECTION
  // Anki capture routes through selection mode, so only offer it when text
  // selection is compiled in.
  if (core::FeatureCatalog::isEnabled("anki_support")) {
    items.push_back({MenuAction::ADD_TO_ANKI, StrId::STR_ADD_TO_ANKI});
  }
#endif
#if ENABLE_DICTIONARY
  items.push_back({MenuAction::DICTIONARY, StrId::STR_DICTIONARY});
#endif
#if ENABLE_BOOKMARKS
  items.push_back(
      {MenuAction::BOOKMARK_TOGGLE, isCurrentPageBookmarked ? StrId::STR_REMOVE_BOOKMARK : StrId::STR_ADD_BOOKMARK});
  if (hasBookmarks) {
    items.push_back({MenuAction::VIEW_BOOKMARKS, StrId::STR_VIEW_BOOKMARKS});
    items.push_back({MenuAction::DELETE_BOOKMARKS, StrId::STR_DELETE_BOOKMARKS});
  }
#endif
#if ENABLE_ANNOTATIONS
  items.push_back({MenuAction::VIEW_HIGHLIGHTS, StrId::STR_HIGHLIGHTS});
#endif
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  return items;
}

void EpubReaderMenuActivity::onEnter() {
  Activity::onEnter();
  // Capture the framebuffer before we render the menu over it. The reader's last
  // page render is still in the buffer at this point; ControlsOptionsActivity will
  // use it to keep the book text visible in the top half while settings are open.
  const size_t bufSize = renderer.getBufferSize();
  // The saved page is a luxury (half-screen preview under the options panels);
  // don't take a 48KB bite out of an already-low heap for it. Downstream code
  // handles a null buffer by falling back to full-screen settings layouts.
  ReaderMemorySnapshot snapshot{ESP.getFreeHeap(), ESP.getMaxAllocHeap()};
  LOG_INF("RDR", "Pre-menu heap: free=%u largest=%u", snapshot.freeHeap, snapshot.maxAllocHeap);
  if (ReaderOptionsMemoryPolicy::canRetainPreview(snapshot, bufSize)) {
    savedPageBuffer = makeUniqueNoThrow<uint8_t[]>(bufSize);
  }
  if (savedPageBuffer) {
    memcpy(savedPageBuffer.get(), renderer.getFrameBuffer(), bufSize);
    LOG_INF("RDR", "Preview retained");
  } else {
    LOG_WRN("RDR", "Preview dropped: free=%u largest=%u", snapshot.freeHeap, snapshot.maxAllocHeap);
  }
  requestUpdate();
}

void EpubReaderMenuActivity::onExit() { Activity::onExit(); }

void EpubReaderMenuActivity::finishOptionsResult(const ControlsOptionsResult* optionsResult) {
  const bool readerChanged = optionsResult && optionsResult->readerSettingsChanged;
  ActivityResult result;
  result.isCancelled = !readerChanged;
  result.data =
      MenuResult{readerChanged ? static_cast<int>(MenuAction::READER_SETTINGS_CHANGED) : -1, pendingOrientation};
  setResult(std::move(result));
  finish();
}

void EpubReaderMenuActivity::finishForMemoryRecovery() {
  ActivityResult result;
  result.isCancelled = false;
  result.data = MenuResult{static_cast<int>(MenuAction::MEMORY_RECOVERY_REQUESTED), pendingOrientation};
  setResult(std::move(result));
  finish();
}

void EpubReaderMenuActivity::openReaderOptions() {
  ReaderMemorySnapshot snapshot{ESP.getFreeHeap(), ESP.getMaxAllocHeap()};
  LOG_INF("RDR", "Pre-build ReaderOptions heap: free=%u largest=%u", snapshot.freeHeap, snapshot.maxAllocHeap);
  if (savedPageBuffer && !ReaderOptionsMemoryPolicy::canBuildSettings(snapshot)) {
    savedPageBuffer.reset();
  }
  auto refreshFn = savedPageBuffer ? previewRefresh_ : nullptr;
  startActivityForResult(
      std::make_unique<ReaderOptionsActivity>(renderer, mappedInput, savedPageBuffer.get(), refreshFn),
      [this](const ActivityResult& readerResult) {
        const auto* optionsResult = std::get_if<ControlsOptionsResult>(&readerResult.data);
        if (optionsResult && optionsResult->memoryRecoveryRequested) {
          if (savedPageBuffer) {
            savedPageBuffer.reset();
#ifdef SIMULATOR
            LOG_INF("SMOKE", "SMOKE_READER_OPTIONS_RETRY_WITHOUT_PREVIEW");
#endif
            openReaderOptions();
            return;
          }
          finishForMemoryRecovery();
          return;
        }
        finishOptionsResult(optionsResult);
      });
}

void EpubReaderMenuActivity::openControlsOptions() {
  ReaderMemorySnapshot snapshot{ESP.getFreeHeap(), ESP.getMaxAllocHeap()};
  LOG_INF("RDR", "Pre-build ControlsOptions heap: free=%u largest=%u", snapshot.freeHeap, snapshot.maxAllocHeap);
  if (savedPageBuffer && !ReaderOptionsMemoryPolicy::canBuildSettings(snapshot)) {
    savedPageBuffer.reset();
  }
  startActivityForResult(std::make_unique<ControlsOptionsActivity>(renderer, mappedInput, savedPageBuffer.get()),
                         [this](const ActivityResult& controlsResult) {
                           const auto* optionsResult = std::get_if<ControlsOptionsResult>(&controlsResult.data);
                           if (optionsResult && optionsResult->memoryRecoveryRequested) {
                             if (savedPageBuffer) {
                               savedPageBuffer.reset();
#ifdef SIMULATOR
                               LOG_INF("SMOKE", "SMOKE_CTRL_RETRY_WITHOUT_PREVIEW");
#endif
                               openControlsOptions();
                               return;
                             }
                             finishForMemoryRecovery();
                             return;
                           }
                           finishOptionsResult(optionsResult);
                         });
}

void EpubReaderMenuActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(menuItems.size()));
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const auto selectedAction = menuItems[selectedIndex].action;
    if (selectedAction == MenuAction::ROTATE_SCREEN) {
      optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                       pendingOrientation, [this](int idx) {
                         pendingOrientation = idx;
                         requestUpdate();
                       });
      requestUpdate();
      return;
    }

    if (selectedAction == MenuAction::READER_OPTIONS) {
      openReaderOptions();
      return;
    }

    if (selectedAction == MenuAction::CONTROLS_OPTIONS) {
      openControlsOptions();
      return;
    }

    if (selectedAction == MenuAction::SELECT_TEXT || selectedAction == MenuAction::ADD_TO_ANKI) {
      ActivityResult result;
      result.data = MenuResult{static_cast<int>(selectedAction), pendingOrientation};
      result.transferredPageSnapshot = std::move(savedPageBuffer);
      setResult(std::move(result));
      finish();
      return;
    }

    setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation});
    finish();
    return;
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    result.data = MenuResult{-1, pendingOrientation};
    setResult(std::move(result));
    finish();
    return;
  }
}

void EpubReaderMenuActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Landscape orientation: button hints are drawn along a vertical edge, so we
  // reserve a horizontal gutter to prevent overlap with menu content.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  // Inverted portrait: button hints appear near the logical top, so we reserve
  // vertical space to keep the header and list clear.
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 30 : 0;
  // Landscape CW places hints on the left edge; CCW keeps them on the right.
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int hintGutterHeight = isPortraitInverted ? 50 : 0;
  const int contentY = hintGutterHeight;

  // Title
  const std::string truncTitle =
      renderer.truncatedText(UI_12_FONT_ID, title.c_str(), contentWidth - 40, EpdFontFamily::BOLD);
  // Manual centering so we can respect the content gutter.
  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, truncTitle.c_str(), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, truncTitle.c_str(), true, EpdFontFamily::BOLD);

  // Progress summary
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, 45, progressLine.c_str());

  const int startY = 75 + contentY;
  constexpr int lineHeight = 30;

  for (size_t i = 0; i < menuItems.size(); ++i) {
    const int displayY = startY + (static_cast<int>(i) * lineHeight);
    const bool isSelected = (static_cast<int>(i) == selectedIndex);

    if (isSelected) {
      renderer.fillRect(contentX, displayY, contentWidth - 1, lineHeight, true);
    }

    renderer.drawText(UI_10_FONT_ID, contentX + 20, displayY, I18N.get(menuItems[i].labelId), !isSelected);

    if (menuItems[i].action == MenuAction::ROTATE_SCREEN) {
      const char* value = I18N.get(orientationLabels[pendingOrientation]);
      const auto width = renderer.getTextWidth(UI_10_FONT_ID, value);
      renderer.drawText(UI_10_FONT_ID, contentX + contentWidth - 20 - width, displayY, value, !isSelected);
    }
  }

  // Footer / Hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

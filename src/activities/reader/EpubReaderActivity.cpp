#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_system.h>

#include <cmath>
#include <iterator>
#include <limits>

#include "AnkiAddActivity.h"
#include "BookStatsActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderUtils.h"
#include "SpiBusMutex.h"
#include "components/UITheme.h"
#include "features/status_overlay/Layout.h"
#include "features/status_overlay/ReaderContext.h"
#include "fontIds.h"
#include "network/BackgroundWifiService.h"
#include "util/RecentBooksStore.h"
#include "util/ScreenshotUtil.h"

// Defined in main.cpp — triggers deep sleep immediately.
void enterDeepSleep();

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr uint8_t maxPageLoadRetryCount = 1;
constexpr uint32_t minHeapForFontPrewarm = 16000;
constexpr uint16_t DEFAULT_AUTO_PAGE_TURN_INTERVAL_S = 30;
constexpr uint16_t MIN_AUTO_PAGE_TURN_INTERVAL_S = 5;
constexpr uint16_t MAX_AUTO_PAGE_TURN_INTERVAL_S = 120;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

int roundPercent(float percent) { return clampPercent(static_cast<int>(std::lround(percent))); }

uint16_t clampAutoPageTurnIntervalSeconds(const uint16_t seconds) {
  if (seconds < MIN_AUTO_PAGE_TURN_INTERVAL_S) {
    return MIN_AUTO_PAGE_TURN_INTERVAL_S;
  }
  if (seconds > MAX_AUTO_PAGE_TURN_INTERVAL_S) {
    return MAX_AUTO_PAGE_TURN_INTERVAL_S;
  }
  return seconds;
}

}  // namespace

void EpubReaderActivity::resetPageLoadRetryState() {
  pageLoadRetrySpineIndex = -1;
  pageLoadRetryCount = 0;
}

void EpubReaderActivity::renderReaderError(StrId messageId) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 300, I18N.get(messageId), true, EpdFontFamily::BOLD);
  renderer.displayBuffer();
  automaticPageTurnActive = false;
}

void EpubReaderActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderMode(true);

  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      if (nextPageNumber == UINT16_MAX) {
        // UINT16_MAX is an in-memory navigation sentinel for "open previous
        // chapter on its last page". It should never be treated as persisted
        // resume state after sleep or reopen.
        LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
        nextPageNumber = 0;
      }
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
    }
  }
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath(240));

  stats = BookReadingStats::load(epub->getCachePath());
  stats.sessionCount++;
  sessionStartMs = millis();
  stats.save(epub->getCachePath());

  globalStats = GlobalReadingStats::load();
  globalStats.totalSessions++;
  globalStats.save();

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  mappedInput.setReaderMode(false);
  features::status_overlay::clearReaderContext();

  // Flush any pending progress save before cleanup
  if (section && (lastSavedSpineIndex != currentSpineIndex || lastSavedPage != section->currentPage)) {
    saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
    lastSavedSpineIndex = currentSpineIndex;
    lastSavedPage = section->currentPage;
  }

  Activity::onExit();

  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  if (epub) {
    const unsigned long elapsedMs = millis() - sessionStartMs;
    if (elapsedMs >= 3000UL) {
      const uint32_t elapsedSecs = static_cast<uint32_t>(elapsedMs / 1000UL);
      stats.totalReadingSeconds += elapsedSecs;
      globalStats.totalReadingSeconds += elapsedSecs;
    }
    stats.save(epub->getCachePath());
    globalStats.save();
  }

  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  if (automaticPageTurnActive) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      automaticPageTurnActive = false;
      // updates chapter title space to indicate page turn disabled
      requestUpdate();
      return;
    }

    if (!section) {
      requestUpdate();
      return;
    }

    // Skips page turn if renderingMutex is busy
    if (RenderLock::peek()) {
      lastPageTurnTime = millis();
      return;
    }

    if ((millis() - lastPageTurnTime) >= pageTurnDuration) {
      pageTurn(true);
      return;
    }
  }

  // Long-press Confirm: execute quick action instead of opening reader menu.
  constexpr unsigned long longPressMenuMs = 600;
  if (SETTINGS.longPressMenuAction != CrossPointSettings::LONG_PRESS_MENU_ACTION::LONG_MENU_OFF &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= longPressMenuMs) {
    executeLongPressMenuAction();
    return;
  }

  // Enter reader menu activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int currentPage = section ? section->currentPage + 1 : 0;
    const int totalPages = section ? section->pageCount : 0;
    float bookProgress = 0.0f;
    if (epub->getBookSize() > 0 && section && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
    }
    const int bookProgressPercent = roundPercent(bookProgress);
    startActivityForResult(std::make_unique<EpubReaderMenuActivity>(
                               renderer, mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
                               SETTINGS.orientation, !currentPageFootnotes.empty()),
                           [this](const ActivityResult& result) {
                             // Always apply orientation change even if the menu was cancelled
                             const auto& menu = std::get<MenuResult>(result.data);
                             applyOrientation(menu.orientation);
                             if (!result.isCancelled) {
                               onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
                             }
                           });
  }

  // Long press BACK (1s+) goes to file selection
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= ReaderUtils::GO_HOME_MS) {
    activityManager.goToFileBrowser(epub ? epub->getPath() : "");
    return;
  }

  // Short press BACK goes directly to home (or restores position if viewing footnote)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_HOME_MS) {
    if (footnoteDepth > 0) {
      restoreSavedPosition();
      return;
    }
    activityManager.goHome();
    return;
  }

  // Side button long-press: change font size (handled before page turn detection)
  if (SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_FONT_SIZE &&
      mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      if (SETTINGS.fontSize < CrossPointSettings::FONT_SIZE_COUNT - 1) {
        SETTINGS.fontSize++;
        SETTINGS.saveToFile();
        {
          RenderLock lock(*this);
          section.reset();
        }
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      if (SETTINGS.fontSize > 0) {
        SETTINGS.fontSize--;
        SETTINGS.saveToFile();
        {
          RenderLock lock(*this);
          section.reset();
        }
        requestUpdate();
      }
      return;
    }
  }

  if (executeLongPowerButtonAction()) {
    return;
  }
  if (executeShortPowerButtonAction()) {
    return;
  }

  auto [prevTriggered, nextTriggered, fromSideBtn] = ReaderUtils::detectPageTurn(mappedInput);
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // At end of the book, forward button goes home and back button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (nextTriggered) {
      onGoHome();
    } else {
      currentSpineIndex = epub->getSpineItemsCount() - 1;
      nextPageNumber = 0;
      pendingPageJump = std::numeric_limits<uint16_t>::max();
      requestUpdate();
    }
    return;
  }

  const bool longPress = mappedInput.getHeldTime() > ReaderUtils::SKIP_HOLD_MS;

  // Don't skip chapter after screenshot
  if (gpio.peekReleased(HalGPIO::BTN_POWER) && gpio.peekReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  const bool chapterSkip = fromSideBtn
      ? SETTINGS.sideButtonLongPress == CrossPointSettings::SIDE_LONG_PRESS::SIDE_LONG_CHAPTER_SKIP
      : SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP;
  if (longPress && chapterSkip) {
    // We don't want to delete the section mid-render, so grab the semaphore
    lastPageTurnTime = millis();
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex = nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
      section.reset();
    }
    requestUpdate();
    return;
  }

  if (longPress && !fromSideBtn && SETTINGS.longPressButtonBehavior == SETTINGS.ORIENTATION_CHANGE) {
    const uint8_t newOrientation =
        nextTriggered ? (SETTINGS.orientation - 1 + SETTINGS.ORIENTATION_COUNT) % SETTINGS.ORIENTATION_COUNT
                      : (SETTINGS.orientation + 1) % SETTINGS.ORIENTATION_COUNT;
    applyOrientation(newOrientation);
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so renderScreen() reloads and repositions on the target spine.
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    section.reset();
  }
}

void EpubReaderActivity::reindexCurrentSection() {
  SETTINGS.saveToFile();
  {
    RenderLock lock(*this);
    GUI.drawPopup(renderer, tr(STR_INDEXING));
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action) {
  using S = CrossPointSettings;
  switch (action) {
    case S::LONG_MENU_SLEEP:
      enterDeepSleep();
      break;
    case S::LONG_MENU_CHANGE_FONT:
      SETTINGS.fontFamily = (SETTINGS.fontFamily + 1) % S::FONT_FAMILY_COUNT;
      reindexCurrentSection();
      break;
    case S::LONG_MENU_REFRESH_SCREEN:
      pagesUntilFullRefresh = 1;
      requestUpdate();
      break;
    case S::LONG_MENU_SYNC_PROGRESS:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SYNC);
      break;
    case S::LONG_MENU_SCREENSHOT:
      onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction::SCREENSHOT);
      break;
    case S::LONG_MENU_CYCLE_PAGE_TURN:
      // Toggle auto page turn on/off; more granular cycling requires PAGE_TURN_INTERVALS (Phase 4 follow-up)
      if (automaticPageTurnActive) {
        setAutoPageTurnIntervalSeconds(0);
      } else {
        setAutoPageTurnIntervalSeconds(DEFAULT_AUTO_PAGE_TURN_INTERVAL_S);
      }
      requestUpdate();
      break;
    case S::LONG_MENU_FILE_TRANSFER:
      openFileTransfer();
      break;
    case S::LONG_MENU_READING_STATS: {
      BookReadingStats displayStats = stats;
      displayStats.totalReadingSeconds +=
          static_cast<uint32_t>((millis() - sessionStartMs) / 1000UL);
      startActivityForResult(
          std::make_unique<BookStatsActivity>(renderer, mappedInput, epub->getTitle(), displayStats, globalStats),
          [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case S::LONG_MENU_OFF:
    default:
      break;
  }
}

void EpubReaderActivity::executeLongPressMenuAction() {
  executeReaderQuickAction(static_cast<CrossPointSettings::LONG_PRESS_MENU_ACTION>(SETTINGS.longPressMenuAction));
}

bool EpubReaderActivity::executeShortPowerButtonAction() {
  if (!mappedInput.wasReleased(MappedInputManager::Button::Power) ||
      mappedInput.getHeldTime() >= SETTINGS.getPowerButtonLongPressDuration()) {
    return false;
  }
  using S = CrossPointSettings;
  switch (SETTINGS.shortPwrBtn) {
    case S::TOGGLE_FONT:
      executeReaderQuickAction(S::LONG_MENU_CHANGE_FONT);
      return true;
    case S::TOGGLE_GUIDE_DOTS:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_GUIDE_DOTS);
      return true;
    case S::TOGGLE_BIONIC_READING:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_BIONIC);
      return true;
    case S::TOGGLE_BOOKMARK:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_BOOKMARK);
      return true;
    case S::SYNC_PROGRESS:
      executeReaderQuickAction(S::LONG_MENU_SYNC_PROGRESS);
      return true;
    case S::MARK_FINISHED:
      executeReaderQuickAction(S::LONG_MENU_MARK_FINISHED);
      return true;
    case S::READING_STATS:
      executeReaderQuickAction(S::LONG_MENU_READING_STATS);
      return true;
    case S::SCREENSHOT:
      executeReaderQuickAction(S::LONG_MENU_SCREENSHOT);
      return true;
    case S::CYCLE_PAGE_TURN:
      executeReaderQuickAction(S::LONG_MENU_CYCLE_PAGE_TURN);
      return true;
    case S::FILE_TRANSFER:
      executeReaderQuickAction(S::LONG_MENU_FILE_TRANSFER);
      return true;
    default:
      return false;
  }
}

bool EpubReaderActivity::executeLongPowerButtonAction() {
  if (!mappedInput.wasReleased(MappedInputManager::Button::Power) ||
      mappedInput.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
    return false;
  }
  using S = CrossPointSettings;
  switch (SETTINGS.longPwrBtn) {
    case S::SLEEP:
      enterDeepSleep();
      return true;
    case S::TOGGLE_FONT:
      executeReaderQuickAction(S::LONG_MENU_CHANGE_FONT);
      return true;
    case S::TOGGLE_GUIDE_DOTS:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_GUIDE_DOTS);
      return true;
    case S::TOGGLE_BIONIC_READING:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_BIONIC);
      return true;
    case S::TOGGLE_BOOKMARK:
      executeReaderQuickAction(S::LONG_MENU_TOGGLE_BOOKMARK);
      return true;
    case S::SYNC_PROGRESS:
      executeReaderQuickAction(S::LONG_MENU_SYNC_PROGRESS);
      return true;
    case S::MARK_FINISHED:
      executeReaderQuickAction(S::LONG_MENU_MARK_FINISHED);
      return true;
    case S::READING_STATS:
      executeReaderQuickAction(S::LONG_MENU_READING_STATS);
      return true;
    case S::SCREENSHOT:
      executeReaderQuickAction(S::LONG_MENU_SCREENSHOT);
      return true;
    case S::CYCLE_PAGE_TURN:
      executeReaderQuickAction(S::LONG_MENU_CYCLE_PAGE_TURN);
      return true;
    case S::FILE_TRANSFER:
      executeReaderQuickAction(S::LONG_MENU_FILE_TRANSFER);
      return true;
    default:
      return false;
  }
}

void EpubReaderActivity::openFileTransfer() {
  if (epub && section) {
    saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
  }
  activityManager.goToFileTransfer(epub ? epub->getPath() : std::string{});
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      const int spineIdx = currentSpineIndex;
      const int currentPage = section ? section->currentPage : 0;
      const int totalPages = section ? section->pageCount : 0;
      const std::string path = epub->getPath();
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, path, spineIdx, currentPage,
                                                               totalPages),
          [this](const ActivityResult& result) {
            if (!result.isCancelled && currentSpineIndex != std::get<ChapterResult>(result.data).spineIndex) {
              RenderLock lock(*this);
              currentSpineIndex = std::get<ChapterResult>(result.data).spineIndex;
              nextPageNumber = 0;
              section.reset();
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      startActivityForResult(std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInput, currentPageFootnotes),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 const auto& footnoteResult = std::get<FootnoteResult>(result.data);
                                 navigateToHref(footnoteResult.href, true);
                               }
                               requestUpdate();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = roundPercent(bookProgress);
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN:
      startActivityForResult(
          std::make_unique<IntervalSelectionActivity>(
              renderer, mappedInput, "AutoPageTurnInterval", StrId::STR_AUTO_TURN_PAGES_PER_MIN,
              StrId::STR_AUTO_TURN_STEP_HINT, getAutoPageTurnIntervalSeconds(),
              MIN_AUTO_PAGE_TURN_INTERVAL_S, MAX_AUTO_PAGE_TURN_INTERVAL_S, 1, 5,
              StrId::STR_NONE_OPT, true, true),
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              setAutoPageTurnIntervalSeconds(
                  static_cast<uint16_t>(std::get<IntervalResult>(result.data).value));
            }
            requestUpdate();
          });
      break;
    case EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN:
      break;
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string fullText;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                const auto& words = line.getBlock()->getWords();
                for (const auto& w : words) {
                  if (!fullText.empty()) fullText += " ";
                  fullText += w;
                }
              }
            }
          }
          if (!fullText.empty()) {
            startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                   [this](const ActivityResult& result) {});
            break;
          }
        }
      }
      // If no text or page loading failed, just close menu
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::ADD_TO_ANKI: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        auto p = section->loadPageFromSectionFile();
        if (p) {
          std::string firstWords;
          int wordCount = 0;
          for (const auto& el : p->elements) {
            if (el->getTag() == TAG_PageLine) {
              const auto& line = static_cast<const PageLine&>(*el);
              if (line.getBlock()) {
                for (const auto& w : line.getBlock()->getWords()) {
                  if (!firstWords.empty()) firstWords += " ";
                  firstWords += w;
                  if (++wordCount >= 10) break;
                }
              }
            }
            if (wordCount >= 10) break;
          }
          if (!firstWords.empty()) {
            startActivityForResult(
                std::make_unique<AnkiAddActivity>(renderer, mappedInput, firstWords, epub->getTitle()),
                [](const ActivityResult&) {});
            break;
          }
          LOG_WRN("EPUB", "ADD_TO_ANKI: no text found on current page");
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      activityManager.goHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      activityManager.goHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      if (KOREADER_STORE.hasCredentials()) {
        const int currentPage = section ? section->currentPage : nextPageNumber;
        const int totalPages = section ? section->pageCount : cachedChapterTotalPageCount;
        std::optional<uint16_t> paragraphIndex;
        if (section && currentPage >= 0 && currentPage < section->pageCount) {
          const uint16_t paragraphPage =
              currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
          if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
            paragraphIndex = pIdx;
          }
        }

        // Pre-compute local KO position and chapter name while Epub is still in RAM.
        CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
        if (paragraphIndex.has_value()) {
          localPos.paragraphIndex = *paragraphIndex;
          localPos.hasParagraphIndex = true;
        }
        KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
        const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
        std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
        const std::string savedEpubPath = epub->getPath();

        // Persist current position so the reader resumes at the right page on return.
        // goToReader() depends on this file, so abort the sync if the write fails.
        if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
          LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
          pendingSyncSaveError = true;
          requestUpdate();
          return;
        }

        // Release Epub and Section to free ~65KB RAM for the TLS handshake.
        LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
        {
          RenderLock lock(*this);
          if (section) {
            nextPageNumber = section->currentPage;
          }
          section.reset();
          epub.reset();
        }
        LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

        activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
            renderer, mappedInput, savedEpubPath, currentSpineIndex, currentPage, totalPages, std::move(localKoPos),
            std::move(localChapterName), paragraphIndex));
      }
      break;
    }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    if (!SETTINGS.saveToFile()) {
      LOG_WRN("EPUB", "Failed to persist orientation setting to SD card");
    }

    // Update renderer orientation to match the new logical coordinate system.
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

uint16_t EpubReaderActivity::getAutoPageTurnIntervalSeconds() const {
  const uint16_t seconds = static_cast<uint16_t>(pageTurnDuration / 1000UL);
  if (seconds == 0) {
    return DEFAULT_AUTO_PAGE_TURN_INTERVAL_S;
  }
  return clampAutoPageTurnIntervalSeconds(seconds);
}

void EpubReaderActivity::setAutoPageTurnIntervalSeconds(uint16_t seconds) {
  if (seconds == 0) {
    automaticPageTurnActive = false;
    return;
  }

  seconds = clampAutoPageTurnIntervalSeconds(seconds);
  lastPageTurnTime = millis();
  pageTurnDuration = static_cast<unsigned long>(seconds) * 1000UL;
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

void EpubReaderActivity::pageTurn(bool isForwardTurn) {
  if (isForwardTurn) {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        section.reset();
      }
    }
  } else {
    if (section->currentPage > 0) {
      section->currentPage--;
    } else if (currentSpineIndex > 0) {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        pendingPageJump = std::numeric_limits<uint16_t>::max();
        currentSpineIndex--;
        section.reset();
      }
    }
  }
  stats.totalPagesTurned++;
  globalStats.totalPagesTurned++;
  lastPageTurnTime = millis();
  requestUpdate();
}

void EpubReaderActivity::render(RenderLock&& lock) {
  if (!epub) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Error: No book loaded", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  // Guard: check spine bounds
  const int spineItemsCount = epub->getSpineItemsCount();
  const auto showPendingSyncSaveError = [this]() {
    if (!pendingSyncSaveError) return;
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  };
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  if (currentSpineIndex > spineItemsCount) {
    currentSpineIndex = spineItemsCount;
  }

  // Show end of book screen
  if (currentSpineIndex == spineItemsCount) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  // Guard: ensure current spine index is within valid range
  if (currentSpineIndex < 0 || currentSpineIndex >= spineItemsCount) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, "Error: Invalid chapter", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin + features::status_overlay::topInset();
  orientedMarginLeft += SETTINGS.screenMargin;
  orientedMarginRight += SETTINGS.screenMargin;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  static constexpr uint8_t STATUS_BAR_TEXT_PADDING = 3;

  // reserves space for automatic page turn indicator when no status bar or progress bar only
  if (automaticPageTurnActive &&
      (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight())) {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin,
                 static_cast<uint8_t>(statusBarHeight + UITheme::getInstance().getMetrics().statusBarVerticalMargin +
                                      STATUS_BAR_TEXT_PADDING));
  } else {
    orientedMarginBottom +=
        std::max(SETTINGS.screenMargin, static_cast<uint8_t>(statusBarHeight + STATUS_BAR_TEXT_PADDING));
  }
  orientedMarginBottom += features::status_overlay::bottomInset();

  const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

  if (!section) {
    LOG_DBG("ERS", "Loading file: %s, index: %d", epub->getSpineItem(currentSpineIndex).href.c_str(),
            currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                  SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                  SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering,
                                  SETTINGS.focusReadingEnabled, SETTINGS.guideReadingEnabled)) {
      LOG_DBG("ERS", "Cache not found, building...");

      GUI.drawPopup(renderer, tr(STR_INDEXING));

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                      SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                      SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering,
                                      SETTINGS.focusReadingEnabled, SETTINGS.guideReadingEnabled,
                                      {this, &EpubReaderActivity::showLoadingPopupTrampoline})) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        section.reset();
        resetPageLoadRetryState();
        showPendingSyncSaveError();
        renderReaderError(StrId::STR_LOAD_EPUB_FAILED);
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (pendingPageJump.has_value()) {
      if (*pendingPageJump >= section->pageCount && section->pageCount > 0) {
        section->currentPage = section->pageCount - 1;
      } else {
        section->currentPage = *pendingPageJump;
      }
      pendingPageJump.reset();
    } else {
      section->currentPage = nextPageNumber;
      if (section->currentPage < 0) {
        section->currentPage = 0;
      } else if (section->currentPage >= section->pageCount && section->pageCount > 0) {
        LOG_DBG("ERS", "Clamping cached page %d to %d", section->currentPage, section->pageCount - 1);
        section->currentPage = section->pageCount - 1;
      }
    }

    if (!pendingAnchor.empty()) {
      if (const auto page = section->getPageForAnchor(pendingAnchor)) {
        section->currentPage = *page;
        LOG_DBG("ERS", "Resolved anchor '%s' to page %d", pendingAnchor.c_str(), *page);
      } else {
        LOG_DBG("ERS", "Anchor '%s' not found in section %d", pendingAnchor.c_str(), currentSpineIndex);
      }
      pendingAnchor.clear();
    }

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d)", section->currentPage, section->pageCount);
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
    renderStatusBar();
    renderer.displayBuffer();
    automaticPageTurnActive = false;
    showPendingSyncSaveError();
    return;
  }

  {
    std::unique_ptr<Page> p;
    {
      SpiBusMutex::Guard guard;
      p = section->loadPageFromSectionFile();
    }
    if (!p) {
      if (pageLoadRetrySpineIndex != currentSpineIndex) {
        pageLoadRetrySpineIndex = currentSpineIndex;
        pageLoadRetryCount = 0;
      }

      if (pageLoadRetryCount < maxPageLoadRetryCount) {
        pageLoadRetryCount++;
        LOG_ERR("ERS", "Failed to load page from SD - clearing section cache and retrying (%u/%u)",
                static_cast<unsigned>(pageLoadRetryCount), static_cast<unsigned>(maxPageLoadRetryCount));
        section->clearCache();
        section.reset();
        requestUpdate();
        automaticPageTurnActive = false;
        return;
      }

      LOG_ERR("ERS", "Failed to load page from SD after retry; showing error");
      section->clearCache();
      section.reset();
      resetPageLoadRetryState();
      renderReaderError(StrId::STR_PAGE_LOAD_ERROR);
      automaticPageTurnActive = false;
      showPendingSyncSaveError();
      return;
    }
    resetPageLoadRetryState();

    // Collect footnotes from the loaded page
    currentPageFootnotes = std::move(p->footnotes);

    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  silentIndexNextChapterIfNeeded(viewportWidth, viewportHeight);

  if (currentSpineIndex != lastSavedSpineIndex || section->currentPage != lastSavedPage) {
    saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
    lastSavedSpineIndex = currentSpineIndex;
    lastSavedPage = section->currentPage;
  }

  showPendingSyncSaveError();

  if (pendingScreenshot) {
    pendingScreenshot = false;
    ScreenshotUtil::takeScreenshot(renderer);
  }
}

void EpubReaderActivity::silentIndexNextChapterIfNeeded(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Build the next chapter cache while the penultimate page is on screen.
  if (section->currentPage != section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

  Section nextSection(epub, nextSpineIndex, renderer);
  if (nextSection.loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                  SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                  SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering,
                                  SETTINGS.focusReadingEnabled, SETTINGS.guideReadingEnabled)) {
    return;
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                     SETTINGS.extraParagraphSpacing, SETTINGS.forceParagraphIndents,
                                     SETTINGS.paragraphAlignment, viewportWidth, viewportHeight,
                                     SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, SETTINGS.imageRendering,
                                     SETTINGS.focusReadingEnabled, SETTINGS.guideReadingEnabled)) {
    LOG_ERR("ERS", "Failed silent indexing for chapter: %d", nextSpineIndex);
  }
}

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  return EpubReaderUtils::saveProgress(*epub, spineIndex, currentPage, pageCount);
}

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  const auto t0 = millis();

  // Font prewarm: scan pass accumulates text, then prewarm, then real render
  auto* fcm = renderer.getFontCacheManager();
  const uint32_t heapBefore = esp_get_free_heap_size();
  std::optional<FontCacheManager::PrewarmScope> prewarmScope;
  if (heapBefore >= minHeapForFontPrewarm) {
    prewarmScope.emplace(fcm->createPrewarmScope());
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);  // scan pass
    prewarmScope->endScanAndPrewarm();
  } else {
    LOG_WRN("ERS", "Skipping font prewarm: heap=%lu", heapBefore);
  }
  fcm->logStats("prewarm");
  const auto tPrewarm = millis();

#if LOG_LEVEL >= 2
  const uint32_t heapAfter = esp_get_free_heap_size();
  LOG_DBG("ERS", "Heap: before=%lu after=%lu delta=%ld", heapBefore, heapAfter,
          (int32_t)heapAfter - (int32_t)heapBefore);
#endif
  // Force special handling for pages with images when anti-aliasing is on
  bool imagePageWithAA = page->hasImages() && SETTINGS.textAntiAliasing;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
  renderStatusBar();
  const auto tBwRender = millis();

  if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking (pablohc's technique):
    // HALF_REFRESH sets particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes.
    // Step 1: Display page with image area blanked (text appears, image area white)
    // Step 2: Re-render with images and display again (images appear clean)
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      renderer.fillRect(imgX + orientedMarginLeft, imgY + orientedMarginTop, imgW, imgH, false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      // Status bar is not re-rendered here to avoid reading stale dynamic values (e.g. battery %)
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, orientedMarginTop);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    // Double FAST_REFRESH handles ghosting for image pages; don't count toward full refresh cadence
  } else {
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
  }
  const auto tDisplay = millis();

  // Grayscale rendering - only for fonts that include grayscale glyph data.
  // Skipped in dark mode: the EPD grayscale LUT assumes a normal-polarity starting state;
  // after a dark-mode BW refresh the pixel polarity is inverted, which confuses the waveform
  // and produces ghosting artefacts.
  const int fontId = SETTINGS.getReaderFontId();
  if (SETTINGS.textAntiAliasing && !renderer.isDarkMode() && renderer.fontSupportsGrayscale(fontId)) {
    const bool bwBufferStored = renderer.storeBwBuffer();
    const auto tBwStore = millis();
    if (!bwBufferStored) {
      const auto tEnd = millis();
      LOG_WRN("ERS", "Skipping grayscale render: BW buffer allocation failed");
      LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums total=%lums",
              tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tEnd - t0);
      return;
    }

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleLsbBuffers();
    const auto tGrayLsb = millis();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, fontId, orientedMarginLeft, orientedMarginTop);
    renderer.copyGrayscaleMsbBuffers();
    const auto tGrayMsb = millis();

    // display grayscale part
    renderer.displayGrayBuffer();
    const auto tGrayDisplay = millis();
    renderer.setRenderMode(GfxRenderer::BW);
    // restore the bw data
    renderer.restoreBwBuffer();
    const auto tBwRestore = millis();

    const auto tEnd = millis();
    LOG_DBG("ERS",
            "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
            "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
            tPrewarm - t0, tBwRender - tPrewarm, tDisplay - tBwRender, tBwStore - tDisplay, tGrayLsb - tBwStore,
            tGrayMsb - tGrayLsb, tGrayDisplay - tGrayMsb, tBwRestore - tGrayDisplay, tEnd - t0);
  } else {
    const auto tEnd = millis();
    LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", tPrewarm - t0,
            tBwRender - tPrewarm, tDisplay - tBwRender, tEnd - t0);
  }
}

void EpubReaderActivity::renderStatusBar() const {
  // Calculate progress in book
  const float sectionChapterProg =
      (section->pageCount > 0) ? static_cast<float>(section->currentPage) / section->pageCount : 0.0f;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  const int currentPage = section ? section->currentPage + 1 : 0;
  const int pageCount = section ? section->pageCount : 0;

  std::string title;

  int textYOffset = 0;

  if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(pageTurnDuration / 1000) + "s";

    // calculates textYOffset when rendering title in status bar
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();

    // offsets text if no status bar or progress bar only
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
      const auto tocItem = epub->getTocItem(tocIndex);
      title = tocItem.title;
    }

  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub->getTitle();
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset);
}

void EpubReaderActivity::navigateToHref(const std::string& hrefStr, const bool savePosition) {
  if (!epub) return;

  // Push current position onto saved stack
  if (savePosition && section && footnoteDepth < MAX_FOOTNOTE_DEPTH) {
    savedPositions[footnoteDepth] = {currentSpineIndex, section->currentPage};
    footnoteDepth++;
    LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", footnoteDepth, currentSpineIndex, section->currentPage);
  }

  // Extract fragment anchor (e.g. "#note1" or "chapter2.xhtml#note1")
  std::string anchor;
  const auto hashPos = hrefStr.find('#');
  if (hashPos != std::string::npos && hashPos + 1 < hrefStr.size()) {
    anchor = hrefStr.substr(hashPos + 1);
  }

  // Check for same-file anchor reference (#anchor only)
  bool sameFile = !hrefStr.empty() && hrefStr[0] == '#';

  int targetSpineIndex;
  if (sameFile) {
    targetSpineIndex = currentSpineIndex;
  } else {
    targetSpineIndex = epub->resolveHrefToSpineIndex(hrefStr);
  }

  if (targetSpineIndex < 0) {
    LOG_DBG("ERS", "Could not resolve href: %s", hrefStr.c_str());
    if (savePosition && footnoteDepth > 0) footnoteDepth--;  // undo push
    return;
  }

  {
    RenderLock lock(*this);
    pendingAnchor = std::move(anchor);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    section.reset();
  }
  requestUpdate();
  LOG_DBG("ERS", "Navigated to spine %d for href: %s", targetSpineIndex, hrefStr.c_str());
}

void EpubReaderActivity::restoreSavedPosition() {
  if (footnoteDepth <= 0) return;
  footnoteDepth--;
  const auto& pos = savedPositions[footnoteDepth];
  LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", footnoteDepth, pos.spineIndex, pos.pageNumber);

  {
    RenderLock lock(*this);
    currentSpineIndex = pos.spineIndex;
    nextPageNumber = pos.pageNumber;
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::showLoadingPopupTrampoline(void* ctx) {
  const auto* const self = static_cast<EpubReaderActivity*>(ctx);
  GUI.drawPopup(self->renderer, tr(STR_INDEXING));
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->pageCount;
    if (epub && epub->getBookSize() > 0 && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      info.progressPercent = roundPercent(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f);
    }
  }
  return info;
}

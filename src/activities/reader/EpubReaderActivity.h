#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <optional>
#include <string>

#include <FeatureFlags.h>

#if ENABLE_READING_STATS
#include "BookReadingStats.h"
#endif
#include "CrossPointSettings.h"
#include "EpubReaderMenuActivity.h"
#if ENABLE_READING_STATS
#include "GlobalReadingStats.h"
#endif
#include "activities/Activity.h"

class EpubReaderActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  // Set when navigating to a footnote href with a fragment (e.g. #note1).
  // Cleared on the next render after the new section loads and resolves it to a page.
  std::string pendingAnchor;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  unsigned long lastPageTurnTime = 0UL;
  unsigned long pageTurnDuration = 0UL;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool pendingScreenshot = false;
  bool pendingSyncSaveError = false;
  bool skipNextButtonCheck = false;  // Skip button processing for one frame after subactivity exit
  bool automaticPageTurnActive = false;
#if ENABLE_READING_STATS
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  unsigned long sessionStartMs = 0UL;
  int completionTriggerSpineIndex = -1;
  float completionTriggerSpineProgress = 1.0f;
  bool completionPromptQueued = false;
  bool completionPromptShown = false;
#endif  // ENABLE_READING_STATS
  int pageLoadRetrySpineIndex = -1;
  uint8_t pageLoadRetryCount = 0;

  std::vector<FootnoteEntry> currentPageFootnotes;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;

  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
#if ENABLE_READING_STATS
  bool pendingReadFolderMove = false;

  struct ReadFolderMoveParams {
    std::string epubPath;
    std::string cachePath;
    std::string title;
  };
  static void readFolderMoveTask(void* arg);

  void setBookCompleted(bool isCompleted);
#endif

  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar() const;
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void reindexCurrentSection();
  void executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action);
  void executeLongPressMenuAction();
  bool executeShortPowerButtonAction();
  bool executeLongPowerButtonAction();
  void openFileTransfer();
  void applyOrientation(uint8_t orientation);
  void setAutoPageTurnIntervalSeconds(uint16_t seconds);
  uint16_t getAutoPageTurnIntervalSeconds() const;
  void pageTurn(bool isForwardTurn);
  float getCurrentBookProgressPercent() const;
#if ENABLE_READING_STATS
  void initializeCompletionPromptTrigger();
  bool isAtOrPastCompletionTrigger() const;
  void queueCompletionPromptIfNeeded();
#endif
  void resetPageLoadRetryState();
  void renderReaderError(StrId messageId);
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();
  static void showLoadingPopupTrampoline(void* ctx);

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
  bool blocksBackgroundServer() override { return true; }
  ScreenshotInfo getScreenshotInfo() const override;
};

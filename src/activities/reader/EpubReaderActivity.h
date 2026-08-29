#pragma once

#include <Epub.h>
#include <Epub/FootnoteEntry.h>
#include <Epub/Section.h>
#include <FeatureFlags.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#if ENABLE_READING_STATS
#include "BookReadingStats.h"
#endif
#include "CrossPointSettings.h"
#include "EpubReaderMenuActivity.h"
#include "ProgressMapper.h"
#if ENABLE_TEXT_SELECTION
#include "SelectionCapturePolicy.h"
#include "SelectionModel.h"
#endif
#if ENABLE_READING_STATS
#include "GlobalReadingStats.h"
#endif
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#if ENABLE_PER_BOOK_SETTINGS
#include "util/BookSettingsOverride.h"
#endif

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
  bool previewRenderOnly = false;
  bool heapDirtyFromIndexing_ = false;
  unsigned long heapDefragRetryAfterMs_ = 0;
  uint8_t heapDefragReclaimAttempts_ = 0;
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
#if ENABLE_BOOKMARKS
  enum class BookmarkFeedbackType : uint8_t { None, Added, Removed, LimitReached };
  BookmarkFeedbackType bookmarkFeedbackType = BookmarkFeedbackType::None;
  unsigned long bookmarkFeedbackShowTime = 0UL;
  bool pendingBookmarkFeedback = false;
#endif  // ENABLE_BOOKMARKS

  std::vector<FootnoteEntry> currentPageFootnotes;

#if ENABLE_PER_BOOK_SETTINGS
  BookSettingsOverride bookOverride;
  bool bookOverrideApplied = false;
#endif

#if ENABLE_TEXT_SELECTION
  // --- Text selection mode (highlight cursor) ---
  // Entered from the reader menu or the long-press quick action. Word rects are
  // collected from the current page's cached layout; the cursor moves word by
  // word (Up/Down), Confirm anchors then extends, second Confirm opens actions.
  bool selectionMode = false;
  selection::Model selModel;
  std::vector<selection::SelWord> selectionPageIndex;
  std::optional<selection::PageGenerationKey> selectionPageGeneration;
  std::vector<selection::HighlightRect> selectionPreviousRuns;
  std::vector<selection::HighlightRect> selectionCurrentRuns;
  OptionPopup selectionPopup;
  std::unique_ptr<uint8_t[]> selectionBaseSnapshot;
  bool selectionSnapshotFallback = false;
  std::unique_ptr<uint8_t[]> pendingSelectionSnapshot;
  uint8_t selectionContentLoads = 0;
  bool selectionNeedsWordReload = false;
  bool selectionOverlayInitialized = false;
  bool selectionForceFullRedraw = true;
  selection_capture::Action selectionPreferredAction = selection_capture::Action::BookNotes;

  static constexpr size_t kMaxSelectionWords = 768;
  static constexpr size_t kMaxSelectionTextBytes = 12 * 1024;

  void enterSelectionMode(std::unique_ptr<uint8_t[]> transferredSnapshot = {});
  bool refreshSelectionWords();
  selection::PageGenerationKey currentSelectionGeneration() const;
  bool buildSelectionPageIndex(const Page& page, int marginTop, int marginRight, int marginBottom, int marginLeft);
  void invalidateSelectionPageIndex();
  void selectionTurnPage(bool forward);
  bool tryCaptureSelectionSnapshotFromFramebuffer();
  void exitSelectionMode();
  bool handleSelectionInput();
  void drawSelectionOverlay(bool incremental);
  void drawSelectionRuns(const std::vector<selection::HighlightRect>& runs) const;
  std::string selectedText() const;
  std::string selectionLocation() const;
  void openSelectionActions();
  // Shared by selection mode and annotation rendering: flatten the page's
  // selectable words into screen rects.
  bool collectSelectableWords(const Page& page, int marginLeft, int marginTop, std::vector<selection::SelWord>& out,
                              bool bounded) const;
#if ENABLE_ANNOTATIONS
  // Draw persistent highlights for the current page into the BW framebuffer.
  void renderAnnotations(const Page& page, int marginLeft, int marginTop) const;
#endif
#else
  // Feature disabled: keep the call site in loop() trivial.
  bool handleSelectionInput() { return false; }
#endif  // ENABLE_TEXT_SELECTION
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
  static void moveReadFolder(ReadFolderMoveParams* params);

  void setBookCompleted(bool isCompleted);
#endif

  // Returns true when it left the panel refreshing (only possible if mayDeferRefresh and
  // the page took the plain-text path). The caller then owns finishDisplayBuffer(), and
  // must not touch the framebuffer until it has called it.
  [[nodiscard]] bool renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                                    int orientedMarginBottom, int orientedMarginLeft, bool mayDeferRefresh);
  void renderStatusBar() const;
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  bool saveProgress(int spineIndex, int currentPage, int pageCount);
  bool persistOrientationSelection(uint8_t orientation);
  void reclaimAfterIndexPressure();
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
#if ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS
  void exportCurrentBookHighlights();
#endif
  void reindexCurrentSection();
  void executeReaderQuickAction(CrossPointSettings::LONG_PRESS_MENU_ACTION action);
  void executeLongPressMenuAction();
  bool executeShortPowerButtonAction();
  bool executeLongPowerButtonAction();
#if ENABLE_DOUBLE_TAP_ACTION
  bool executeDoubleTapAction();
#endif
  void openFileTransfer();
  // Returns true if sync acted (launched, or surfaced a save error); false if it was a no-op
  // because no KOReader credentials are stored.
  bool launchKOReaderSync();
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
  // Free what is reclaimable before refusing to render. Caller must hold the RenderLock.
  void reclaimHeapForRender();
  void renderReaderError(StrId messageId);
  void refreshReaderPreviewBuffer(uint8_t* dest, size_t size);
  void navigateToHref(const std::string& href, bool savePosition = false);
  void restoreSavedPosition();
  static void showLoadingPopupTrampoline(void* ctx);

  bool pendingSilentIndexing = false;
  uint16_t cachedViewportWidth = 0;
  uint16_t cachedViewportHeight = 0;
  // Acquires the rendering mutex, then runs the build. Call from loop() only.
  void performDeferredSilentIndexing();
  // The build itself. Caller MUST already hold a RenderLock -- renderingMutex is not
  // recursive, so taking it twice on one task aborts. render() uses this one.
  void performDeferredSilentIndexingLocked();
  // True while a button is held or an edge is queued. Used to back off from starting a
  // multi-second chapter build when the reader is actively turning pages.
  [[nodiscard]] bool inputIsPending() const;

  bool pendingCoverThumbBake_ = false;
  unsigned long lastReaderInputMs_ = 0;
  void queueCoverThumbBakeIfIdle();

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub)
      : Activity("EpubReader", renderer, mappedInput), epub(std::move(epub)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;
  bool isReaderActivity() const override { return true; }
  bool blocksBackgroundServer() override { return true; }
  // Auto page turn is a hands-off reading mode, so it registers no input and
  // the idle sleep timer used to kill the session mid-book.
  bool preventAutoSleep() override { return automaticPageTurnActive; }
  ScreenshotInfo getScreenshotInfo() const override;
};

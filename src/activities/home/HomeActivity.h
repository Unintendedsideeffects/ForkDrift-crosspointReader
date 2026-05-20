#pragma once
#include <array>
#include <functional>
#include <optional>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

struct Rect;

class HomeActivity final : public Activity {
 public:
  static constexpr int kCarouselFrameCount = 1;
  static constexpr int kMaxCachedBooks = 3;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int selectedMenuIndex = 0;
  int selectedBookIndex = 0;
  int menuItemCount = 0;
  int menuOpenBookIndex = -1;
  int menuMyLibraryIndex = -1;
  int menuOpdsIndex = -1;
  int menuTodoIndex = -1;
  int menuAnkiIndex = -1;
  int menuFileTransferIndex = -1;
  int menuSettingsIndex = -1;

  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool inButtonGrid = false;
  bool firstRenderDone = false;
  bool hasOpdsUrl = false;
  bool hasCoverImage = false;
  bool hasContinueReading = false;
  bool updateRequired = false;

  // Carousel state
  int lastCarouselBookIndex = 0;
  bool carouselFramesReady = false;
  bool carouselWarmupPending = false;
  bool bookProgressCached = false;
  std::array<float, kMaxCachedBooks> cachedBookProgress{};
  uint8_t* carouselFrames[kCarouselFrameCount] = {};

  // Static cover cache — persists across HomeActivity instances to avoid reloading
  // covers from SD on every home visit. Invalidated when the recent book list changes.
  // Cost: 48KB heap held while reading; benefit: instant home re-entry.
  static bool coverRendered;
  static bool coverBufferStored;
  static uint8_t* coverBuffer;
  static std::vector<std::string> coverCacheBookPaths;

  std::string lastBookTitle;
  std::string lastBookAuthor;
  std::string coverBmpPath;
  float currentBookProgressPercent = -1.0f;
  std::vector<RecentBook> recentBooks;
  void onContinueReading();
  void onMyLibraryOpen();
  void onNotesOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onTodoOpen();
  void onAnkiOpen();

  void freeCoverBuffer();          // Free the stored cover buffer
  bool isCoverCacheValid() const;  // True if static cover buffer matches current recent books
  void freeCarouselFrames();
  bool allocateCarouselFrameSlots(int targetFrameCount);
  bool buildCarouselCacheFile(const std::string& cacheKey, uint64_t cacheKeyHash, int bookCount,
                              bool showProgressPopup = false);
  bool loadCarouselFrameFromDisk(uint64_t cacheKeyHash, int bookCount, int bookIdx, int slotIdx);
  int chooseCarouselEvictionSlot(int centerIdx, int bookCount,
                                 std::optional<int> protectedBookIdx = std::nullopt) const;
  void renderCarouselFrameToCurrentBuffer(int bookIdx, float* outProgressPercent);
  void renderCarouselFrame(int bookIdx, int slotIdx);
  void updateSlidingWindowCache(int centerIdx, int bookCount);
  bool preRenderCarouselFrames(bool showProgressPopup = false);
  void loadBookProgress();

 protected:
  int getMenuItemCount() const;
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void loadRecentBooks();
  void loadRecentCovers(int coverHeight);
  void openSelectedBook();
  void rebuildMenuLayout();
  bool isPokemonPartyHomeMode() const;
  std::string getMenuItemLabel(int index) const;
  bool drawCoverAt(const std::string& coverPath, int x, int y, int width, int height) const;

  static std::string fallbackTitleFromPath(const std::string& path);
  static std::string fallbackAuthor(const RecentBook& book);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

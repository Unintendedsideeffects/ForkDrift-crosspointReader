#pragma once
#include <Epub.h>

#include <memory>

#include "../Activity.h"
#include "../ActivityResult.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator{700, 700};
  int currentSpineIndex = 0;
  int currentPage = 0;
  int totalPagesInSpine = 0;
  int selectorIndex = 0;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Total TOC items count
  int getTotalItems() const;

  bool hasSyncOption() const;
  int tocIndexFromItemIndex(int itemIndex) const;
  bool isSyncItem(int index) const;
  void launchSyncActivity();
  void onSyncPosition(int newSpineIndex, int newPage);

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex, const int currentPage = 0,
                                              const int totalPagesInSpine = 0)
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex),
        currentPage(currentPage),
        totalPagesInSpine(totalPagesInSpine) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

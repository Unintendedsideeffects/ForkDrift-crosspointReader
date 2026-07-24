#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "activities/books/TabView.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

// MyLibraryActivity doubles as a standalone Recent/Files browser (goToMyLibrary /
// goToFileBrowser) and, in `embedded` mode, as the Files tab of the unified
// library strip (BooksTabActivity). Embedded mode locks to the Files tab, hands
// horizontal navigation + the Up-at-top gesture to the host strip, and drops its
// own header/tab-bar chrome so the strip overlay owns the top of the screen.
class MyLibraryActivity final : public Activity, public TabView {
 public:
  enum class Tab { Recent, Files };
  enum class ViewMode { List, Grid };

  // Matches BooksTabActivity::kTabStripHeight; embedded content starts below it.
  static constexpr int kStripInset = 48;

 private:
  // Deletion
  bool pendingSubActivityExit = false;
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  Tab currentTab = Tab::Recent;
  ViewMode viewMode = ViewMode::List;
  bool embedded = false;  // hosted as the Files tab of the library strip

  // Recent tab state
  std::vector<RecentBook> recentBooks;

  // Files tab state (from FileSelectionActivity)
  std::string basepath = "/";
  std::vector<std::string> files;

  // Data loading
  void loadRecentBooks();
  void loadFiles();
  size_t findEntry(const std::string& name) const;

  // Rendering
  void renderRecentTab(int contentTop, int contentHeight) const;
  void renderFilesTab(int contentTop, int contentHeight) const;
  void renderGrid() const;
  bool drawCoverAt(const std::string& path, int x, int y, int width, int height) const;

  struct GridMetrics {
    int cols;
    int rows;
    int thumbWidth;
    int thumbHeight;
    int paddingX;
    int paddingY;
    int startX;
    int startY;
  };
  GridMetrics getGridMetrics() const;

  int getCurrentItemCount() const {
    return currentTab == Tab::Recent ? static_cast<int>(recentBooks.size()) : static_cast<int>(files.size());
  }
  int getPageItems() const;

 public:
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                             bool embedded = false)
      : Activity("MyLibrary", renderer, mappedInput),
        embedded(embedded),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override { return true; }

  // TabView: reuse the Activity lifecycle so the strip hosts this with no second
  // rendering path. Files-locked embedded mode enters the strip only at the root
  // directory's top row, so the Up gesture never steals an ordinary list scroll.
  void enter() override { onEnter(); }
  void exit() override { onExit(); }
  Activity* asActivity() override { return this; }
  bool atNavigationTop() const override { return basepath == "/" && selectorIndex == 0; }
};

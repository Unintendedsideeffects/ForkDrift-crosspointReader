#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

class MyLibraryActivity final : public Activity {
 public:
  enum class Tab { Recent, Files };
  enum class ViewMode { List, Grid };

 private:
  // Deletion
  bool pendingSubActivityExit = false;
  void clearFileMetadata(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  Tab currentTab = Tab::Recent;
  ViewMode viewMode = ViewMode::List;

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
  explicit MyLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/")
      : Activity("MyLibrary", renderer, mappedInput), basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override { return true; }
};

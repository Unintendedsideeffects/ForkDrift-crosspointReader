#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/books/TabView.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

class RecentBooksGridActivity final : public Activity, public TabView {
 public:
  static constexpr int BOOKS_PER_PAGE = 9;
  static constexpr int MAX_GRID_BOOKS = BOOKS_PER_PAGE * 2;
  static constexpr int COVER_HEIGHT = 180;
  static constexpr int COVER_WIDTH = 123;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<RecentBook> recentBooks;
  std::vector<float> recentBookProgress;
  std::vector<bool> recentBookProgressLoaded;
  int loadedPageStart = -1;
  int previousSelectorIndex = -1;
  int coverX[BOOKS_PER_PAGE] = {0};
  int coverY[BOOKS_PER_PAGE] = {0};
  int coverW[BOOKS_PER_PAGE] = {0};
  int coverH[BOOKS_PER_PAGE] = {0};

  void loadRecentBooks();
  void loadPageCovers(int pageStart);
  void ensureProgressLoaded(int index);

 public:
  explicit RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooksGrid", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void enter() override { onEnter(); }
  void exit() override { onExit(); }
  Activity* asActivity() override { return this; }
  bool atNavigationTop() const override { return recentBooks.empty() || selectorIndex / 3 == 0; }
};

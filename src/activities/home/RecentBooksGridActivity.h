#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

class RecentBooksGridActivity final : public Activity {
 public:
  static constexpr int BOOKS_PER_PAGE = 9;
  static constexpr int COVER_HEIGHT = 180;
  static constexpr int COVER_WIDTH = 123;

  explicit RecentBooksGridActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooksGrid", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  std::vector<RecentBook> recentBooks;
  int loadedPageStart = -1;

  void loadRecentBooks();
  void loadPageCovers(int pageStart);
};

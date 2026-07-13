#pragma once

#include <FeatureFlags.h>

#if ENABLE_BOOKS_TAB_UI

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "activities/books/BooksTabModel.h"
#include "activities/books/TabView.h"

class BooksTabActivity final : public Activity {
 public:
  explicit BooksTabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BooksTab", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override;
  bool preventAutoSleep() override;

 private:
  static constexpr size_t kBooksTab = 2;
  static constexpr size_t kOpdsTab = 3;
  static constexpr int kTabStripHeight = 48;

  std::vector<books_tab_model::BooksTab> tabs;
  std::unique_ptr<TabView> activeChild;
  size_t selectedTab = kBooksTab;
  bool stripFocused = false;

  static void launchEmbedded(void* context, std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);
  void bindEmbeddedLauncher();
  void createChild();
  void destroyChild();
  void selectAdjacentTab(int direction);
};

#endif  // ENABLE_BOOKS_TAB_UI

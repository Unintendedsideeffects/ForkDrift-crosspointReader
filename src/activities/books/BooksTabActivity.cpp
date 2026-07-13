#include "BooksTabActivity.h"

#if ENABLE_BOOKS_TAB_UI

#include <I18n.h>

#include "OpdsServerStore.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/home/RecentBooksGridActivity.h"
#include "components/TabStrip.h"
#include "components/themes/BaseTheme.h"

void BooksTabActivity::onEnter() {
  Activity::onEnter();
  OPDS_STORE.loadFromFile();
  tabs = books_tab_model::make(tr(STR_BOOKS_TAB_RECENT), tr(STR_BOOKS_TAB_FILES), tr(STR_BOOKS_TAB_BOOKS),
                               tr(STR_BOOKS_TAB_OPDS), tr(STR_BOOKS_TAB_SETTINGS));
  selectedTab = kBooksTab;
  stripFocused = false;
  createChild();
  requestUpdate();
}

void BooksTabActivity::onExit() {
  Activity::setEmbeddedActivityLauncher(nullptr, nullptr, nullptr);
  destroyChild();
  tabs.clear();
  Activity::onExit();
}

void BooksTabActivity::bindEmbeddedLauncher() {
  auto* activity = activeChild->asActivity();
  Activity::setEmbeddedActivityLauncher(activity, this, &BooksTabActivity::launchEmbedded);
}

void BooksTabActivity::launchEmbedded(void* context, std::unique_ptr<Activity>&& activity,
                                      ActivityResultHandler resultHandler) {
  auto* host = static_cast<BooksTabActivity*>(context);
  host->startActivityForResult(std::move(activity),
                               [host, handler = std::move(resultHandler)](const ActivityResult& result) {
                                 if (host->activeChild) {
                                   handler(result);
                                 }
                               });
}

void BooksTabActivity::destroyChild() {
  if (activeChild) {
    activeChild->exit();
    activeChild.reset();
  }
}

void BooksTabActivity::createChild() {
  destroyChild();

  if (selectedTab == kBooksTab) {
    activeChild = std::make_unique<RecentBooksGridActivity>(renderer, mappedInput);
  } else if (selectedTab == kOpdsTab) {
    const auto& servers = OPDS_STORE.getServers();
    const OpdsServer server = servers.empty() ? OpdsServer{} : servers.front();
    activeChild = std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, server);
  } else {
    return;
  }

  bindEmbeddedLauncher();
  activeChild->enter();
}

void BooksTabActivity::selectAdjacentTab(const int direction) {
  const size_t next = books_tab_model::move(tabs, selectedTab, direction);
  if (next == selectedTab) {
    return;
  }
  selectedTab = next;
  createChild();
  requestUpdate();
}

void BooksTabActivity::loop() {
  if (stripFocused) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      selectAdjacentTab(-1);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      selectAdjacentTab(1);
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
               mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      stripFocused = false;
      requestUpdate();
    }
    return;
  }

  if (activeChild && activeChild->atNavigationTop() && mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    stripFocused = true;
    requestUpdate();
    return;
  }

  if (activeChild) {
    activeChild->loop();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }
}

void BooksTabActivity::render(RenderLock&& lock) {
  if (activeChild) {
    activeChild->render(std::move(lock));
  } else {
    renderer.clearScreen();
  }

  drawTabStrip(renderer, Rect{0, 0, renderer.getScreenWidth(), kTabStripHeight}, tabs, selectedTab);
  renderer.displayBuffer();
}

bool BooksTabActivity::blocksBackgroundServer() { return selectedTab == kOpdsTab; }

bool BooksTabActivity::preventAutoSleep() { return selectedTab == kOpdsTab; }

#endif  // ENABLE_BOOKS_TAB_UI

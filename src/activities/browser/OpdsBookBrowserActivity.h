#pragma once

#include <OpdsParser.h>

#include <string>
#include <utility>
#include <vector>

#include "OpdsServerStore.h"
#include "activities/Activity.h"
#include "activities/books/TabView.h"
#include "util/ButtonNavigator.h"

/**
 * Activity for browsing and downloading books from a configured OPDS server
 * (calibre_sync / OPDS Support). Navigates the catalog hierarchy and downloads EPUBs.
 */
class OpdsBookBrowserActivity final : public Activity, public TabView {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR, SEARCH_INPUT };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server)
      : Activity("OpdsBookBrowser", renderer, mappedInput), buttonNavigator(), server(std::move(server)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void enter() override { onEnter(); }
  void exit() override { onExit(); }
  Activity* asActivity() override { return this; }
  bool atNavigationTop() const override { return state == BrowserState::BROWSING && selectorIndex == 0; }
  bool blocksBackgroundServer() override { return true; }

 private:
  BrowserState state = BrowserState::LOADING;
  std::vector<OpdsEntry> entries;
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  std::string searchTemplate;
  bool consumeConfirm = false;
  bool consumeBack = false;
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  ButtonNavigator buttonNavigator;

  OpdsServer server;  // Copied at construction — safe even if the store changes during browsing

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
  void launchSearch();
  void performSearch(const std::string& query);

  // Phase 1 (plan 023): browse a cached root catalog offline; SYNC connects + refreshes;
  // network-needing actions (download/navigate/search) connect on demand.
  enum class PendingAction { None, Download, Navigate, Search };
  PendingAction pendingAction = PendingAction::None;
  OpdsEntry pendingEntry;    // params for a deferred Download / Navigate
  std::string pendingQuery;  // param for a deferred Search
  bool showingCachedCatalog = false;
  unsigned long confirmPressStartMs = 0;  // for long-press-Confirm = SYNC

  bool isOnline() const;
  bool loadCachedCatalog();  // populate `entries` from the persisted root shelf; true if any
  void syncCatalog();        // connect-on-demand, then refresh the root feed
  // If offline, stash a deferred action + launch WiFi selection; returns true (caller returns).
  // If already online, returns false (caller proceeds with the action).
  bool deferUntilOnline(PendingAction action, const OpdsEntry& entry, const std::string& query);

  bool preventAutoSleep() override { return state == BrowserState::LOADING || state == BrowserState::DOWNLOADING; }
};

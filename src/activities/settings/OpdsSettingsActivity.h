#pragma once

#include "OpdsServerStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Edit screen for one OPDS server entry (calibre_sync / OPDS Support):
 * name, URL, username, password, and delete. Used for add and edit flows.
 */
class OpdsSettingsActivity final : public Activity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1)
      : Activity("OpdsSettings", renderer, mappedInput), serverIndex(serverIndex) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;
  int serverIndex;
  OpdsServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveServer();
};

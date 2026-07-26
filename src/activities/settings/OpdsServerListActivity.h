#pragma once

#include "activities/Activity.h"
#include "activities/books/TabView.h"
#include "util/ButtonNavigator.h"

/**
 * Lists configured OPDS servers (calibre_sync / OPDS Support).
 * Add, edit, or delete servers; pickerMode opens OpdsBookBrowserActivity instead of the editor.
 * In `embedded` mode it is the Settings tab of the unified library strip: it drops
 * its own header (the strip owns the top) and Back returns home instead of finishing.
 */
class OpdsServerListActivity final : public Activity, public TabView {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false,
                                  bool embedded = false)
      : Activity("OpdsServerList", renderer, mappedInput), pickerMode(pickerMode), embedded(embedded) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // TabView: reuse the Activity lifecycle so the strip hosts this with no second
  // rendering path. A flat list, so the strip is entered from the top row.
  void enter() override { onEnter(); }
  void exit() override { onExit(); }
  Activity* asActivity() override { return this; }
  bool atNavigationTop() const override { return selectedIndex == 0; }

 private:
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool pickerMode = false;
  bool embedded = false;

  int getItemCount() const;
  void handleSelection();
};

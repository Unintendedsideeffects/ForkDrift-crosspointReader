#pragma once

#include "activities/Activity.h"

/**
 * On-device submenu for KOReader Sync (koreader_sync feature):
 * username, password, and credential test.
 */
class KOReaderSettingsActivity final : public Activity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("KOReaderSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool showsStatusBarIp() const override { return true; }

 private:
  size_t selectedIndex = 0;
  void handleSelection();
};

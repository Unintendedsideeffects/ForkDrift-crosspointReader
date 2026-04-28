#pragma once

#include "activities/Activity.h"

/**
 * Submenu for OPDS Browser settings.
 * Shows OPDS Server URL and HTTP authentication options.
 */
class CalibreSettingsActivity final : public Activity {
 public:
  explicit CalibreSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("CalibreSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool showsStatusBarIp() const override { return true; }

 private:
  size_t selectedIndex = 0;
  void handleSelection();
};

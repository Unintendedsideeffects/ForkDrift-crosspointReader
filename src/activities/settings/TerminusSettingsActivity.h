#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * On-device Terminus setup.
 *
 * The device identity and model are derived locally; users only enter the
 * Terminus server URL and API token they receive from their Terminus server.
 */
class TerminusSettingsActivity final : public Activity {
 public:
  explicit TerminusSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TerminusSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kMenuItems = 3;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::string baseUrl;
  std::string apiToken;
  std::string errorMessage;

  void handleSelection();
  void saveSetup();
};

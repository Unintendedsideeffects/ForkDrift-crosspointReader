#pragma once
#include <atomic>

#include "SettingInfo.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;
  // The first paint after entering Settings must fully refresh the panel to clear
  // ghosting left by the screen we came from (Home); later in-place updates stay
  // on FAST_REFRESH to avoid a black/white flash on every keypress.
  bool firstRenderDone = false;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  static constexpr int categoryCount = 4;
  static const StrId categoryNames[categoryCount];

 protected:
  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  void rebuildSettingsLists();
  void openSleepTimeoutPicker();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override { return true; }
  bool showsStatusBarIp() const override { return true; }
};

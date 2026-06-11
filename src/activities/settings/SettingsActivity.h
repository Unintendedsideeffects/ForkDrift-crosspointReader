#pragma once
#include <array>
#include <atomic>

#include "SettingInfo.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;
  bool firstRenderDone = false;

 public:
  static constexpr int categoryCount = 6;

 private:
  // Per-category settings derived from shared list + device-only actions
  std::array<std::vector<SettingInfo>, categoryCount> settingsByCategory;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  std::vector<SettingInfo> cachedMasterSettings;

 public:
  static const StrId categoryNames[categoryCount];

 protected:
  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  void rebuildSettingsLists();
  void invalidateMasterSettingsCache();
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

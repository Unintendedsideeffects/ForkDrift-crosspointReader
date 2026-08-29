#pragma once
#include <vector>

#include "SettingInfo.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class SettingsActivity final : public Activity {
  OptionPopup optionPopup;
  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;

 public:
  static constexpr int categoryCount = 6;

 private:
  std::vector<SettingInfo> visibleSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  std::vector<const char*> dependencyKeys;

  bool keyAffectsVisibility(const char* key) const;

 public:
  static const StrId categoryNames[categoryCount];

 protected:
  void enterCategory(int categoryIndex);
  void toggleCurrentSetting();
  void openSleepTimeoutPicker();
  void rebuildSettingsLists();

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

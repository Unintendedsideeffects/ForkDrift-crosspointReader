#pragma once
#include <I18n.h>

#include <vector>

#include "../Activity.h"
#include "../settings/SettingsActivity.h"
#include "util/ButtonNavigator.h"

class ControlsOptionsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int settingsCount = 0;
  bool lowMemory_ = false;
  std::vector<SettingInfo> settings;
  bool readerSettingsChanged_ = false;
  const uint8_t* pageBuffer_ = nullptr;

  void rebuildSettingsList();
  void moveSelection(bool forward);
  void toggleCurrentSetting();

 public:
  explicit ControlsOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const uint8_t* pageBuffer = nullptr)
      : Activity("ControlsOptions", renderer, mappedInput), pageBuffer_(pageBuffer) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#pragma once

#include <functional>

#include "../Activity.h"
#include "SettingInfo.h"
#include "util/ButtonNavigator.h"

using ReaderPreviewRefreshFn = std::function<void(uint8_t* buffer, size_t bufferSize)>;

class ReaderOptionsActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  int settingsCount = 0;
  std::vector<SettingInfo> settings;
  bool readerSettingsChanged_ = false;
  uint8_t* pageBuffer_ = nullptr;
  ReaderPreviewRefreshFn previewRefresh_;

  bool rebuildSettingsList();
  void moveSelection(bool forward);
  void toggleCurrentSetting();
  static bool settingAffectsPreview(const SettingInfo& setting);
  void refreshPreviewIfNeeded(const SettingInfo& setting);

 public:
  explicit ReaderOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t* pageBuffer = nullptr,
                                 ReaderPreviewRefreshFn previewRefresh = nullptr)
      : Activity("ReaderOptions", renderer, mappedInput),
        pageBuffer_(pageBuffer),
        previewRefresh_(std::move(previewRefresh)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

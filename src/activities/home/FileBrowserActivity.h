#pragma once

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  std::string basepath = "/";
  std::vector<std::string> files;
  size_t selectorIndex = 0;
  bool lockLongPressBack = false;

  void loadFiles();
  void clearFileMetadata(const std::string& fullPath);
  void onSelectBook(const std::string& fullPath);
  void onGoHome();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FileBrowser", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

#pragma once
#include <FeatureFlags.h>

#if ENABLE_LUA_PLUGINS

#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

struct PluginInfo {
  std::string name;
  std::string description;
};

class PluginListActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::vector<PluginInfo> luaPlugins;
  std::function<void()> onGoBack;
  std::function<void(const std::string& name)> onLaunchPlugin;

 public:
  PluginListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                     std::function<void(const std::string& name)> onLaunchPlugin, std::function<void()> onGoBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};

#endif

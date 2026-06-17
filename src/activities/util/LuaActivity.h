#pragma once
#include <FeatureFlags.h>

#if ENABLE_LUA_PLUGINS

#include <string>

#include "../Activity.h"

class LuaActivity final : public Activity {
 public:
  LuaActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& pluginName,
              std::function<void()> onGoBack);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override {}  // unused: draw() called directly from loop()

 private:
  std::string pluginName;
  std::function<void()> onGoBack;
  bool scriptLoaded = false;
  bool inputReady = false;  // true once all buttons released after launch

  void showError(const char* msg);
};

#endif

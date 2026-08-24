#include "features/lua_plugins/Registration.h"

#include <FeatureFlags.h>

#include <new>

#include "core/registries/HomeActionRegistry.h"

#if ENABLE_LUA_PLUGINS
#include "activities/ActivityManager.h"
#include "activities/util/LuaActivity.h"
#include "activities/util/PluginListActivity.h"
#endif

namespace features::lua_plugins {

#if ENABLE_LUA_PLUGINS
namespace {

bool shouldExposePluginsHomeAction(core::HomeActionEntry::HomeActionContext ctx) {
  (void)ctx;
  return true;
}

Activity* createPluginsHomeActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, void* callbackCtx,
                                          void (*onBack)(void* ctx)) {
  // The plugin list is the activity Home starts; launching an individual plugin
  // then happens from inside it, so it pushes rather than starting-for-result.
  auto onLaunchPlugin = [&renderer, &mappedInput](const std::string& name) {
    activityManager.pushActivity(
        std::make_unique<LuaActivity>(renderer, mappedInput, name, [] { activityManager.popActivity(); }));
  };
  auto onGoBack = [callbackCtx, onBack] {
    if (onBack != nullptr) {
      onBack(callbackCtx);
      return;
    }
    activityManager.popActivity();
  };
  return new (std::nothrow) PluginListActivity(renderer, mappedInput, onLaunchPlugin, onGoBack);
}

}  // namespace
#endif

void registerFeature() {
#if ENABLE_LUA_PLUGINS
  core::HomeActionEntry homeEntry{};
  homeEntry.actionId = "lua_plugins";
  homeEntry.shouldExpose = shouldExposePluginsHomeAction;
  homeEntry.create = createPluginsHomeActionActivity;
  core::HomeActionRegistry::add(homeEntry);
#endif
}

}  // namespace features::lua_plugins

#include "core/features/FeatureLifecycle.h"

#include "core/registries/LifecycleRegistry.h"

namespace core {

void FeatureLifecycle::onStorageReady() { LifecycleRegistry::dispatchStorageReady(); }

void FeatureLifecycle::onSettingsLoaded(GfxRenderer& renderer) { LifecycleRegistry::dispatchSettingsLoaded(renderer); }

void FeatureLifecycle::onFontSetup(GfxRenderer& renderer) { LifecycleRegistry::dispatchFontSetup(renderer); }

void FeatureLifecycle::onBackgroundServerStarted() { LifecycleRegistry::dispatchBackgroundServerStarted(); }

}  // namespace core

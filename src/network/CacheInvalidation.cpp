#include "network/CacheInvalidation.h"

#include "activities/boot_sleep/SleepActivity.h"
#include "core/features/FeatureModules.h"

namespace {

void invalidateSleepCacheIfNeeded(const String& filePath) {
  String lowerPath = filePath;
  lowerPath.toLowerCase();
  if (lowerPath.startsWith("/sleep/") || lowerPath.equals("/sleep")) {
    invalidateSleepImageCache();
  }
}

}  // namespace

void invalidateFeatureCachesIfNeeded(const String& filePath) {
  core::FeatureModules::onWebFileChanged(filePath);
  invalidateSleepCacheIfNeeded(filePath);
}

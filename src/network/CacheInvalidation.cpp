#include "network/CacheInvalidation.h"

#include "activities/boot_sleep/SleepActivity.h"
#include "core/features/FeatureModules.h"

namespace {

void invalidateSleepCacheIfNeeded(const String& filePath) {
  String lowerPath = filePath;
  lowerPath.toLowerCase();
  if (lowerPath.equals("/sleep.bmp") || lowerPath.equals("/sleep.png") || lowerPath.equals("/sleep.jpg") ||
      lowerPath.equals("/sleep.jpeg") || lowerPath.startsWith("/sleep/") || lowerPath.equals("/sleep")) {
    invalidateSleepImageCache();
  }
}

}  // namespace

void invalidateFeatureCachesIfNeeded(const String& filePath) {
  core::FeatureModules::onWebFileChanged(filePath);
  invalidateSleepCacheIfNeeded(filePath);
}

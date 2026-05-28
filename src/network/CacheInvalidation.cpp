#include "network/CacheInvalidation.h"

#include <algorithm>

#include "activities/boot_sleep/SleepActivity.h"
#include "core/features/FeatureModules.h"

namespace {

void invalidateSleepCacheIfNeeded(const String& filePath) {
  String lowerPath = filePath;
#ifndef SIMULATOR
  lowerPath.toLowerCase();
#else
  std::transform(lowerPath.s.begin(), lowerPath.s.end(), lowerPath.s.begin(), ::tolower);
#endif
  if (lowerPath.startsWith("/sleep/") || lowerPath.equals("/sleep")) {
    invalidateSleepImageCache();
  }
}

}  // namespace

void invalidateFeatureCachesIfNeeded(const String& filePath) {
  core::FeatureModules::onWebFileChanged(filePath);
  invalidateSleepCacheIfNeeded(filePath);
}

#pragma once

#include <WString.h>

// Cache invalidation helpers shared across web server handler modules.
// Defined in CacheInvalidation.cpp.

// Invalidates feature-module caches if the given SD path is affected (sleep images, plugin data).
void invalidateFeatureCachesIfNeeded(const String& filePath);

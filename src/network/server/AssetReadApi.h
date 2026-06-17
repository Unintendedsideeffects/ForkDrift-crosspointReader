#pragma once

#include <Arduino.h>

#include <functional>
#include <string>
#include <vector>

#include "util/RecentBooksStore.h"

namespace network {

struct AssetReadResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  String body = "Unhandled asset request";
  std::string resolvedPath;

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

using CoverPathResolver = std::function<bool(const String& bookPath, std::string& coverPath)>;

AssetReadResult resolveCoverAssetPath(const String& rawPath, const std::vector<RecentBook>& books,
                                      const CoverPathResolver& fallbackResolver);

String buildSleepImagesJson();

}  // namespace network

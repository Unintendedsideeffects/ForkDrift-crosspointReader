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

// Recent-book and document cover paths are usually a template ending in
// "thumb_[HEIGHT].bmp" -- one template, one generated file per theme size.
// `thumbSuffixes` lists the concrete substitutions to try, best first; the
// first one present on SD is served. A path without the token is used as-is.
AssetReadResult resolveCoverAssetPath(const String& rawPath, const std::vector<RecentBook>& books,
                                      const CoverPathResolver& fallbackResolver,
                                      const std::vector<std::string>& thumbSuffixes = {});

String buildSleepImagesJson();

}  // namespace network

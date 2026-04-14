#include "network/AssetReadApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "SpiBusMutex.h"
#include "util/PathUtils.h"

namespace network {

AssetReadResult resolveCoverAssetPath(const String& rawPath, const std::vector<RecentBook>& books,
                                      const CoverPathResolver& fallbackResolver) {
  if (rawPath.isEmpty()) {
    return {400, "text/plain", "Missing path"};
  }

  String bookPath = PathUtils::urlDecode(rawPath);
  if (!PathUtils::isValidSdPath(bookPath)) {
    return {400, "text/plain", "Invalid path"};
  }
  bookPath = PathUtils::normalizePath(bookPath);
  if (PathUtils::pathContainsProtectedItem(bookPath)) {
    return {403, "text/plain", "Cannot access protected items"};
  }

  std::string coverPath;
  for (const auto& book : books) {
    if (book.path == bookPath.c_str()) {
      coverPath = book.coverBmpPath;
      break;
    }
  }

  if (coverPath.empty() && fallbackResolver) {
    fallbackResolver(bookPath, coverPath);
  }

  if (coverPath.empty()) {
    return {404, "text/plain", "No cover available"};
  }

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(coverPath.c_str());
  }
  if (!exists) {
    return {404, "text/plain", "Cover file not found"};
  }

  return {200, "image/bmp", "", coverPath};
}

String buildSleepImagesJson() {
#if ENABLE_IMAGE_SLEEP
  const char* allowedExts[] = {".bmp", ".png", ".jpg", ".jpeg"};
#else
  const char* allowedExts[] = {".bmp"};
#endif
  constexpr int numAllowed = sizeof(allowedExts) / sizeof(allowedExts[0]);
  const char* sleepDirs[] = {"/sleep", "/.sleep"};

  String json = "[";
  bool seenFirst = false;
  char output[300];
  JsonDocument doc;

  for (const char* dirName : sleepDirs) {
    FsFile dir;
    {
      SpiBusMutex::Guard guard;
      dir = Storage.open(dirName);
    }
    if (!dir || !dir.isDirectory()) {
      if (dir) {
        SpiBusMutex::Guard guard;
        dir.close();
      }
      continue;
    }

    while (true) {
      std::string entryName;
      bool isEntryDir = false;
      bool done = false;

      {
        SpiBusMutex::Guard guard;
        FsFile file = dir.openNextFile();
        if (!file) {
          done = true;
        } else {
          char name[260];
          file.getName(name, sizeof(name));
          entryName = name;
          isEntryDir = file.isDirectory();
          file.close();
        }
      }

      if (done) break;
      if (isEntryDir || entryName.empty() || entryName[0] == '.') continue;

      std::string lower = entryName;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      bool supported = false;
      for (int i = 0; i < numAllowed; i++) {
        const size_t extLen = std::strlen(allowedExts[i]);
        if (lower.size() >= extLen && lower.substr(lower.size() - extLen) == allowedExts[i]) {
          supported = true;
          break;
        }
      }
      if (!supported) continue;

      doc.clear();
      doc["path"] = std::string(dirName) + "/" + entryName;
      doc["name"] = entryName;
      const size_t written = serializeJson(doc, output, sizeof(output));
      if (written >= sizeof(output)) continue;

      if (seenFirst) {
        json += ",";
      } else {
        seenFirst = true;
      }
      json += output;
    }

    {
      SpiBusMutex::Guard guard;
      dir.close();
    }
  }

  json += "]";
  return json;
}

}  // namespace network

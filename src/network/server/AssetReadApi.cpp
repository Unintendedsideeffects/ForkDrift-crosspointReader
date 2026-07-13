#include "network/server/AssetReadApi.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include "SpiBusMutex.h"
#include "network/server/DirScan.h"
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

namespace {

bool isSupportedSleepImageName(const std::string& filename, const char* const* allowedExts, const int numAllowed) {
  if (filename.empty() || filename[0] == '.') return false;
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (int i = 0; i < numAllowed; i++) {
    const size_t extLen = std::strlen(allowedExts[i]);
    if (lower.size() >= extLen && lower.substr(lower.size() - extLen) == allowedExts[i]) {
      return true;
    }
  }
  return false;
}

std::string joinSleepPath(const std::string& directoryPath, const std::string& entryName) {
  if (entryName.empty()) return directoryPath;
  if (entryName[0] == '/') return entryName;
  if (directoryPath.empty() || directoryPath == "/") return "/" + entryName;
  if (directoryPath.back() == '/') return directoryPath + entryName;
  return directoryPath + "/" + entryName;
}

std::string leafName(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
}

struct SleepImageScanContext {
  const std::string& directoryPath;
  const char* const* allowedExts;
  int numAllowed;
  String& json;
  bool& seenFirst;
  JsonDocument& doc;
  char* output;
  size_t outputSize;
};

// NOLINTNEXTLINE(misc-no-recursion) -- intentional: directory tree traversal
void appendSleepImagesFromDirectory(const std::string& directoryPath, const char* const* allowedExts,
                                    const int numAllowed, String& json, bool& seenFirst, JsonDocument& doc,
                                    char* output, const size_t outputSize);

void handleSleepImageEntry(void* rawContext, const DirScanEntry& scanned) {
  auto& context = *static_cast<SleepImageScanContext*>(rawContext);
  const std::string entryName(scanned.name);
  const std::string fullPath = joinSleepPath(context.directoryPath, entryName);
  const std::string leaf = leafName(entryName);
  if (leaf.empty() || leaf[0] == '.') return;

  if (scanned.isDirectory) {
    appendSleepImagesFromDirectory(fullPath, context.allowedExts, context.numAllowed, context.json, context.seenFirst,
                                   context.doc, context.output, context.outputSize);
    return;
  }

  if (!isSupportedSleepImageName(leaf, context.allowedExts, context.numAllowed)) return;

  context.doc.clear();
  context.doc["path"] = fullPath;
  context.doc["name"] = leaf;
  const size_t written = serializeJson(context.doc, context.output, context.outputSize);
  if (written >= context.outputSize) return;

  if (context.seenFirst) {
    context.json += ",";
  } else {
    context.seenFirst = true;
  }
  context.json += context.output;
}

// NOLINTNEXTLINE(misc-no-recursion) -- intentional: directory tree traversal
void appendSleepImagesFromDirectory(const std::string& directoryPath, const char* const* allowedExts,
                                    const int numAllowed, String& json, bool& seenFirst, JsonDocument& doc,
                                    char* output, const size_t outputSize) {
  HalFile dir;
  {
    SpiBusMutex::Guard guard;
    dir = Storage.open(directoryPath.c_str());
  }
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      SpiBusMutex::Guard guard;
      dir.close();
    }
    return;
  }

  SleepImageScanContext context{directoryPath, allowedExts, numAllowed, json, seenFirst, doc, output, outputSize};
  forEachDirEntry(dir, &context, handleSleepImageEntry);
}

}  // namespace

String buildSleepImagesJson() {
  const char* allowedExts[] = SLEEP_IMAGE_ALLOWED_EXTS;
  constexpr int numAllowed = sizeof(allowedExts) / sizeof(allowedExts[0]);

  String json = "[";
  bool seenFirst = false;
  char output[300];
  JsonDocument doc;

  appendSleepImagesFromDirectory("/sleep", allowedExts, numAllowed, json, seenFirst, doc, output, sizeof(output));

  json += "]";
  return json;
}

}  // namespace network

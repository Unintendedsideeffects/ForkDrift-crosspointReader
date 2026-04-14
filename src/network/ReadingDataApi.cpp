#include "network/ReadingDataApi.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include "SpiBusMutex.h"
#include "network/RecentBookJson.h"
#include "util/BookProgressDataStore.h"
#include "util/PathUtils.h"

namespace network {

ReadingDataHttpResult buildRecentBooksResponse(const std::vector<RecentBook>& books, const bool includePokemon) {
  String body = "[";
  bool seenFirst = false;
  for (const auto& book : books) {
    if (seenFirst) {
      body += ",";
    } else {
      seenFirst = true;
    }
    body += buildRecentBookJson(book, includePokemon);
  }
  body += "]";
  return {200, "application/json", body};
}

ReadingDataHttpResult handleBookProgressRequest(const String& rawPath) {
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

  bool exists = false;
  {
    SpiBusMutex::Guard guard;
    exists = Storage.exists(bookPath.c_str());
  }
  if (!exists) {
    return {404, "text/plain", "Book not found"};
  }

  if (!BookProgressDataStore::supportsBookPath(bookPath.c_str())) {
    return {400, "text/plain", "Unsupported book type"};
  }

  JsonDocument response;
  response["path"] = bookPath;

  BookProgressDataStore::ProgressData progress;
  if (BookProgressDataStore::loadProgress(bookPath.c_str(), progress)) {
    appendBookProgressJson(response["progress"].to<JsonObject>(), progress);
  } else {
    response["progress"] = nullptr;
  }

  String json;
  serializeJson(response, json);
  return {200, "application/json", json};
}

}  // namespace network

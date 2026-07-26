// Host-test stub for JsonSettingsIO.
// Settings serialization (CrossPointSettings save/load) is shared with
// production via SettingsSerializer.
// Obfuscation is omitted: passwords are stored as plaintext in this host-only
// stub because hardware-key obfuscation is not available here.
// All other store functions (state, wifi, koreader, recent) are no-ops.

#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
// Keep this undef as a defensive guard for host builds that include pthread/time headers.
#undef TIME_UTC
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "SettingsSerializer.h"
#include "src/activities/reader/ReadingStatsStore.h"
#include "util/RecentBooksStore.h"

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;
  settings_serializer::toDoc(s, doc);

  std::string jsonStr;
  serializeJson(doc, jsonStr);
  return Storage.writeFile(path, String(jsonStr.c_str()));
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CPS", "JSON parse error: %s", error.c_str());
    return false;
  }
  return settings_serializer::fromDoc(s, doc, needsResave);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, HalFile& file, bool* needsResave) {
  std::string json;
  uint8_t buffer[128];
  while (true) {
    const size_t bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead == 0) {
      break;
    }
    json.append(reinterpret_cast<const char*>(buffer), bytesRead);
  }
  return loadSettings(s, json.c_str(), needsResave);
}

// ---- Stubs for store types not compiled into host tests ----

class CrossPointState;
class WifiCredentialStore;
class RecentBooksStore;

bool JsonSettingsIO::saveState(const CrossPointState&, const char*) { return true; }
bool JsonSettingsIO::loadState(CrossPointState&, const char*) { return false; }
bool JsonSettingsIO::loadState(CrossPointState&, HalFile&) { return false; }
bool JsonSettingsIO::saveWifi(const WifiCredentialStore&, const char*) { return true; }
bool JsonSettingsIO::loadWifi(WifiCredentialStore&, const char*, bool*) { return false; }
bool JsonSettingsIO::loadWifi(WifiCredentialStore&, HalFile&, bool*) { return false; }
bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore&, const char*) { return true; }

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) {
      break;
    }
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(std::move(book));
  }
  return true;
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, HalFile& file) {
  FsFileJsonReader reader(file);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) {
      break;
    }
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(std::move(book));
  }
  return true;
}
bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& srv : store.servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = srv.name;
    obj["url"] = srv.url;
    obj["username"] = srv.username;
    obj["password"] = srv.password;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  if (needsResave) {
    *needsResave = false;
  }

  store.servers.clear();
  const JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.servers.size() >= OpdsServerStore::MAX_SERVERS) {
      break;
    }
    OpdsServer srv;
    srv.name = obj["name"] | std::string("");
    srv.url = obj["url"] | std::string("");
    srv.username = obj["username"] | std::string("");
    srv.password = obj["password"] | std::string("");
    store.servers.push_back(std::move(srv));
  }
  return true;
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, HalFile& file, bool* needsResave) {
  FsFileJsonReader reader(file);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadOpds(store, json.c_str(), needsResave);
}

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["version"] = 1;
  doc["globalPagesTurned"] = store.globalPagesTurned;
  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.books) {
    JsonObject obj = books.add<JsonObject>();
    obj["cachePath"] = book.cachePath;
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["totalPagesTurned"] = book.totalPagesTurned;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["completed"] = book.completed;
    JsonArray days = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = days.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }
  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.readingDays) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }
  store.books.clear();
  store.readingDays.clear();
  store.globalPagesTurned = doc["globalPagesTurned"] | 0UL;
  JsonArrayConst books = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : books) {
    ReadingBookStats book;
    book.cachePath = obj["cachePath"] | std::string("");
    if (book.cachePath.empty()) {
      continue;
    }
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | 0ULL;
    book.sessions = obj["sessions"] | 0UL;
    book.totalPagesTurned = obj["totalPagesTurned"] | 0UL;
    book.lastSessionMs = obj["lastSessionMs"] | 0UL;
    book.lastProgressPercent = obj["lastProgressPercent"] | 0;
    book.completed = obj["completed"] | false;
    JsonArrayConst bookDays = obj["readingDays"].as<JsonArrayConst>();
    for (JsonObjectConst dayObj : bookDays) {
      const uint32_t dayOrdinal = dayObj["dayOrdinal"] | 0UL;
      const uint64_t readingMs = dayObj["readingMs"] | 0ULL;
      if (dayOrdinal != 0 && readingMs != 0) {
        book.readingDays.push_back(ReadingDayStats{dayOrdinal, readingMs});
      }
    }
    store.books.push_back(std::move(book));
  }
  JsonArrayConst days = doc["readingDays"].as<JsonArrayConst>();
  for (JsonObjectConst dayObj : days) {
    const uint32_t dayOrdinal = dayObj["dayOrdinal"] | 0UL;
    const uint64_t readingMs = dayObj["readingMs"] | 0ULL;
    if (dayOrdinal != 0 && readingMs != 0) {
      store.readingDays.push_back(ReadingDayStats{dayOrdinal, readingMs});
    }
  }
  return true;
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, HalFile& file) {
  FsFileJsonReader reader(file);
  JsonDocument doc;
  if (deserializeJson(doc, reader)) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadReadingStats(store, json.c_str());
}

#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "I18n.h"
#include "I18nKeys.h"
#include "OpdsServerStore.h"
#include "SettingsSerializer.h"
#include "activities/reader/ReadingStatsStore.h"
#include "util/RecentBooksStore.h"
#include "util/WifiCredentialStore.h"

namespace {

template <typename Input>
bool deserializeJsonLogged(JsonDocument& doc, Input&& input, const char* tag) {
  const DeserializationError error = deserializeJson(doc, input);
  if (error) {
    LOG_ERR(tag, "JSON parse error: %s", error.c_str());
    return false;
  }
  return true;
}

bool deserializeJsonFromFile(JsonDocument& doc, HalFile& file, const char* tag) {
  FsFileJsonReader reader(file);
  return deserializeJsonLogged(doc, reader, tag);
}

bool loadStateFromDoc(CrossPointState& s, const JsonDocument& doc) {
  s.openEpubPath = doc["openEpubPath"] | std::string("");
  s.lastSleepImage = doc["lastSleepImage"] | (uint8_t)UINT8_MAX;
  s.readerActivityLoadCount = doc["readerActivityLoadCount"] | (uint8_t)0;
  s.lastSleepFromReader = doc["lastSleepFromReader"] | false;
  s.wifiAutoConnectSkipCount = doc["wifiAutoConnectSkipCount"] | (uint8_t)0;
  s.wifiAutoConnectBackoffLevel = doc["wifiAutoConnectBackoffLevel"] | (uint8_t)0;
  s.wifiAutoConnectWaitingForNewCredential = doc["wifiAutoConnectWaitingForNewCredential"] | false;
  s.pendingBookmarkSpine = doc["pendingBookmarkSpine"] | static_cast<uint16_t>(PENDING_BOOKMARK_SPINE_NONE);
  s.pendingBookmarkProgress = doc["pendingBookmarkProgress"] | PENDING_BOOKMARK_PROGRESS_NONE;
  return true;
}

}  // namespace

// ---- CrossPointState ----

bool JsonSettingsIO::saveState(const CrossPointState& s, const char* path) {
  JsonDocument doc;
  doc["openEpubPath"] = s.openEpubPath;
  doc["lastSleepImage"] = s.lastSleepImage;
  doc["readerActivityLoadCount"] = s.readerActivityLoadCount;
  doc["lastSleepFromReader"] = s.lastSleepFromReader;
  doc["wifiAutoConnectSkipCount"] = s.wifiAutoConnectSkipCount;
  doc["wifiAutoConnectBackoffLevel"] = s.wifiAutoConnectBackoffLevel;
  doc["wifiAutoConnectWaitingForNewCredential"] = s.wifiAutoConnectWaitingForNewCredential;
  if (s.pendingBookmarkSpine != PENDING_BOOKMARK_SPINE_NONE) {
    doc["pendingBookmarkSpine"] = s.pendingBookmarkSpine;
    doc["pendingBookmarkProgress"] = s.pendingBookmarkProgress;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadState(CrossPointState& s, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "CPS")) {
    return false;
  }
  return loadStateFromDoc(s, doc);
}

bool JsonSettingsIO::loadState(CrossPointState& s, HalFile& file) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "CPS")) {
    return false;
  }
  return loadStateFromDoc(s, doc);
}

// ---- CrossPointSettings ----

bool JsonSettingsIO::saveSettings(const CrossPointSettings& s, const char* path) {
  JsonDocument doc;
  settings_serializer::toDoc(s, doc);

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "CPS")) {
    return false;
  }
  return settings_serializer::fromDoc(s, doc, needsResave);
}

bool JsonSettingsIO::loadSettings(CrossPointSettings& s, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "CPS")) {
    return false;
  }
  return settings_serializer::fromDoc(s, doc, needsResave);
}

// ---- WifiCredentialStore ----

namespace {

std::string obfuscateForDisk(const std::string& plaintext) {
  return std::string(obfuscation::obfuscateToBase64(plaintext).c_str());
}

std::string deobfuscateFromDisk(const char* encoded, bool* ok) {
  return obfuscation::deobfuscateFromBase64(encoded, ok);
}

// The JSON shape, the integrity checks and the legacy fallback all live in
// wifi_credentials so the host tests can drive them with a plaintext codec;
// only the hardware-key obfuscation is injected from here.
constexpr WifiPasswordCodec HARDWARE_CODEC{&obfuscateForDisk, &deobfuscateFromDisk};

}  // namespace

bool JsonSettingsIO::saveWifi(const WifiCredentialStore& store, const char* path) {
  const std::string json = wifi_credentials::serialize(store.snapshot(), HARDWARE_CODEC);
  return Storage.writeFile(path, String(json.c_str()));
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, const char* json, bool* needsResave) {
  WifiCredentialSnapshot snapshot;
  if (!wifi_credentials::parse(json, HARDWARE_CODEC, snapshot, needsResave)) {
    return false;
  }
  store.adoptSnapshot(std::move(snapshot));
  return true;
}

bool JsonSettingsIO::loadWifi(WifiCredentialStore& store, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "WCS")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadWifi(store, json.c_str(), needsResave);
}

// ---- RecentBooksStore ----

bool JsonSettingsIO::saveRecentBooks(const RecentBooksStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "RBS")) {
    return false;
  }

  store.recentBooks.clear();
  const JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (store.getCount() >= 10) break;
    RecentBook book;
    book.path = obj["path"] | std::string("");
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    store.recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", store.getCount());
  return true;
}

bool JsonSettingsIO::loadRecentBooks(RecentBooksStore& store, HalFile& file) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "RBS")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadRecentBooks(store, json.c_str());
}

// ---- OpdsServerStore ----

bool JsonSettingsIO::saveOpds(const OpdsServerStore& store, const char* path) {
  JsonDocument doc;
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& srv : store.servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = srv.name;
    obj["url"] = srv.url;
    obj["username"] = srv.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(srv.password);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, const char* json, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "OPS")) {
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
    bool ok = false;
    srv.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
    if (!ok || srv.password.empty()) {
      srv.password = obj["password"] | std::string("");
      if (!srv.password.empty() && needsResave) {
        *needsResave = true;
      }
    }
    store.servers.push_back(std::move(srv));
  }

  LOG_DBG("OPS", "Loaded %zu OPDS servers from file", store.servers.size());
  return true;
}

bool JsonSettingsIO::loadOpds(OpdsServerStore& store, HalFile& file, bool* needsResave) {
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "OPS")) {
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

  JsonArray aggregateDays = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.readingDays) {
    JsonObject dayObj = aggregateDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  if (!deserializeJsonLogged(doc, json, "RST")) {
    return false;
  }

  store.books.clear();
  store.readingDays.clear();
  store.lastSessionSnapshot = {};
  store.activeSession = false;
  store.activeBookIndex = 0;
  store.activeAccumulatedMs = 0;
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
    JsonArrayConst days = obj["readingDays"].as<JsonArrayConst>();
    for (JsonObjectConst dayObj : days) {
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
  JsonDocument doc;
  if (!deserializeJsonFromFile(doc, file, "RST")) {
    return false;
  }
  String json;
  serializeJson(doc, json);
  return loadReadingStats(store, json.c_str());
}

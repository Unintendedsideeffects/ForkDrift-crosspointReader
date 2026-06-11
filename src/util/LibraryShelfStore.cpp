#include "LibraryShelfStore.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>
#include <Logging.h>

#include <utility>

namespace {
constexpr char LIBRARY_SHELF_FILE_JSON[] = "/.crosspoint/library_shelf.json";
constexpr size_t MAX_LIBRARY_SHELF_ENTRIES = 6;
}  // namespace

LibraryShelfStore LibraryShelfStore::instance;

std::vector<LibraryShelfEntry> LibraryShelfStore::getSnapshot() const {
  std::lock_guard<std::mutex> lock(shelfMutex);
  return entries;
}

void LibraryShelfStore::replaceEntries(const std::string& nextServerName, std::vector<LibraryShelfEntry> nextEntries) {
  std::lock_guard<std::mutex> lock(shelfMutex);
  if (nextEntries.size() > MAX_LIBRARY_SHELF_ENTRIES) {
    nextEntries.resize(MAX_LIBRARY_SHELF_ENTRIES);
  }
  serverName = nextServerName;
  entries = std::move(nextEntries);
  if (!saveToFileUnlocked()) {
    LOG_DBG("SHELF", "Failed to persist library shelf");
  }
}

bool LibraryShelfStore::saveToFileUnlocked() const {
  JsonDocument doc;
  doc["server"] = serverName;
  JsonArray jsonEntries = doc["entries"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject jsonEntry = jsonEntries.add<JsonObject>();
    jsonEntry["t"] = entry.title;
    jsonEntry["a"] = entry.author;
    jsonEntry["h"] = entry.href;
  }

  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  return Storage.writeFile(LIBRARY_SHELF_FILE_JSON, json);
}

bool LibraryShelfStore::loadFromFile() {
  std::lock_guard<std::mutex> lock(shelfMutex);
  if (!Storage.exists(LIBRARY_SHELF_FILE_JSON)) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("SHELF", LIBRARY_SHELF_FILE_JSON, file)) {
    return false;
  }

  JsonDocument doc;
  FsFileJsonReader reader(file);
  const DeserializationError error = deserializeJson(doc, reader);
  file.close();
  if (error) {
    LOG_DBG("SHELF", "Failed to parse library shelf: %s", error.c_str());
    return false;
  }

  std::vector<LibraryShelfEntry> loadedEntries;
  const JsonArrayConst jsonEntries = doc["entries"].as<JsonArrayConst>();
  loadedEntries.reserve(jsonEntries.size() < MAX_LIBRARY_SHELF_ENTRIES ? jsonEntries.size()
                                                                       : MAX_LIBRARY_SHELF_ENTRIES);
  for (JsonObjectConst jsonEntry : jsonEntries) {
    if (loadedEntries.size() >= MAX_LIBRARY_SHELF_ENTRIES) {
      break;
    }
    loadedEntries.push_back({jsonEntry["t"] | "", jsonEntry["a"] | "", jsonEntry["h"] | ""});
  }

  serverName = doc["server"] | "";
  entries = std::move(loadedEntries);
  return true;
}

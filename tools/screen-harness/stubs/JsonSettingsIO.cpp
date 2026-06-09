#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>

#include <string>

#include "util/RecentBooksStore.h"

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

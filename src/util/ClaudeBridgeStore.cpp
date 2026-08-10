#include "ClaudeBridgeStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {
constexpr size_t kMaxConfigJsonBytes = 4096;

std::string readFileToString(const char* tag, const char* path) {
  HalFile f;
  if (!Storage.openFileForRead(tag, path, f)) {
    return {};
  }
  if (f.size() > kMaxConfigJsonBytes) {
    LOG_ERR(tag, "JSON file too large: %s (%u bytes)", path, static_cast<unsigned>(f.size()));
    f.close();
    return {};
  }
  String buf;
  buf.reserve(256);
  char tmp[64];
  size_t n;
  while ((n = f.read(reinterpret_cast<uint8_t*>(tmp), sizeof(tmp) - 1)) > 0) {
    tmp[n] = '\0';
    buf += tmp;
  }
  return buf.c_str();
}
}  // namespace

ClaudeBridgeStore& ClaudeBridgeStore::getInstance() {
  static ClaudeBridgeStore instance;
  return instance;
}

bool ClaudeBridgeStore::loadFromJson(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    LOG_ERR("CLAUDE", "Failed to parse bridge config JSON");
    return false;
  }
  const char* token = doc["token"] | "";
  if (token[0] == '\0' || strlen(token) > 256) {
    LOG_ERR("CLAUDE", "Bridge config requires a token of 1 to 256 characters");
    return false;
  }
  token_ = token;
  LOG_INF("CLAUDE", "Claude question config loaded");
  return true;
}

bool ClaudeBridgeStore::load() {
  if (Storage.exists(kDropPath)) {
    LOG_INF("CLAUDE", "Found drop file %s — importing", kDropPath);
    const std::string json = readFileToString("CLAUDE", kDropPath);
    if (!json.empty() && loadFromJson(json.c_str())) {
      Storage.remove(kDropPath);
      LOG_INF("CLAUDE", "Drop file consumed");
      return save();
    }
    Storage.remove(kDropPath);
    LOG_ERR("CLAUDE", "Drop file invalid; removed");
  }

  if (!Storage.exists(kStoredPath)) {
    return false;
  }

  const std::string json = readFileToString("CLAUDE", kStoredPath);
  return !json.empty() && loadFromJson(json.c_str());
}

bool ClaudeBridgeStore::save() const {
  JsonDocument doc;
  doc["token"] = token_.c_str();

  String json;
  serializeJson(doc, json);

  Storage.ensureDirectoryExists("/.crosspoint");
  if (!Storage.writeFile(kStoredPath, json)) {
    LOG_ERR("CLAUDE", "Failed to save bridge config to %s", kStoredPath);
    return false;
  }
  return true;
}

void ClaudeBridgeStore::clear() {
  token_.clear();
  if (Storage.exists(kStoredPath)) {
    Storage.remove(kStoredPath);
  }
}

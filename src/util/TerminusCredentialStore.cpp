#include "TerminusCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace {
constexpr size_t kMaxCredentialJsonBytes = 4096;
}

TerminusCredentialStore& TerminusCredentialStore::getInstance() {
  static TerminusCredentialStore instance;
  return instance;
}

bool TerminusCredentialStore::hasCredentials() const { return !apiKey_.empty() && !deviceId_.empty(); }

bool TerminusCredentialStore::loadFromJson(const char* json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    LOG_ERR("TERMINUS", "Failed to parse credentials JSON");
    return false;
  }

  const char* key = doc["api_key"] | "";
  const char* id = doc["device_id"] | "";
  if (key[0] == '\0' || id[0] == '\0') {
    LOG_ERR("TERMINUS", "Credentials JSON missing api_key or device_id");
    return false;
  }

  apiKey_ = key;
  deviceId_ = id;
  deviceModel_ = (doc["device_model"] | "xteink_x4");
  baseUrl_ = (doc["base_url"] | "https://api.trmnl.com");

  // Trim trailing slash from base URL.
  while (!baseUrl_.empty() && baseUrl_.back() == '/') {
    baseUrl_.pop_back();
  }

  LOG_INF("TERMINUS", "Credentials loaded: model=%s", deviceModel_.c_str());
  return true;
}

static std::string readFileToString(const char* tag, const char* path) {
  HalFile f;
  if (!Storage.openFileForRead(tag, path, f)) {
    return {};
  }
  if (f.size() > kMaxCredentialJsonBytes) {
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

bool TerminusCredentialStore::load() {
  // Check for SD card drop file — import and consume it.
  if (Storage.exists(kDropPath)) {
    LOG_INF("TERMINUS", "Found drop file %s — importing", kDropPath);
    const std::string json = readFileToString("TERMINUS", kDropPath);
    if (!json.empty() && loadFromJson(json.c_str())) {
      // Must remove the drop file. No HalFile in scope so no explicit close needed.
      Storage.remove(kDropPath);
      LOG_INF("TERMINUS", "Drop file consumed");
      return save();
    }
    Storage.remove(kDropPath);
    LOG_ERR("TERMINUS", "Drop file invalid; removed");
  }

  if (!Storage.exists(kStoredPath)) {
    return false;
  }

  const std::string json = readFileToString("TERMINUS", kStoredPath);
  return !json.empty() && loadFromJson(json.c_str());
}

bool TerminusCredentialStore::save() const {
  JsonDocument doc;
  doc["api_key"] = apiKey_.c_str();
  doc["device_id"] = deviceId_.c_str();
  doc["device_model"] = deviceModel_.c_str();
  doc["base_url"] = baseUrl_.c_str();

  String json;
  serializeJson(doc, json);

  Storage.ensureDirectoryExists("/.crosspoint");
  if (!Storage.writeFile(kStoredPath, json)) {
    LOG_ERR("TERMINUS", "Failed to save credentials to %s", kStoredPath);
    return false;
  }
  return true;
}

void TerminusCredentialStore::clear() {
  apiKey_.clear();
  deviceId_.clear();
  deviceModel_ = "xteink_x4";
  baseUrl_ = "https://api.trmnl.com";

  if (Storage.exists(kStoredPath)) {
    Storage.remove(kStoredPath);
  }
}

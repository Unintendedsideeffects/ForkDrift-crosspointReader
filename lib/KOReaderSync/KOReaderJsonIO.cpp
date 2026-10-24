#include "KOReaderJsonIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <HttpHeader.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include "KOReaderCredentialStore.h"

namespace {

void writeHeaders(JsonObject obj, const std::array<HttpHeader, KOReaderCredentialStore::MAX_CUSTOM_HEADERS>& headers) {
  JsonArray arr = obj["headers"].to<JsonArray>();
  for (const auto& header : headers) {
    JsonObject entry = arr.add<JsonObject>();
    entry["name"] = header.name;
    entry["value_obf"] = obfuscation::obfuscateToBase64(header.value);
  }
}

void readHeaders(JsonObjectConst obj, std::array<HttpHeader, KOReaderCredentialStore::MAX_CUSTOM_HEADERS>& headers) {
  headers = {};
  if (!obj["headers"].is<JsonArrayConst>()) {
    return;
  }
  size_t slot = 0;
  for (JsonObjectConst entry : obj["headers"].as<JsonArrayConst>()) {
    if (slot >= KOReaderCredentialStore::MAX_CUSTOM_HEADERS) {
      break;
    }
    HttpHeader header;
    header.name = entry["name"] | std::string("");
    bool ok = false;
    header.value = obfuscation::deobfuscateFromBase64(entry["value_obf"] | "", &ok);
    if (!ok) {
      header.value.clear();
    }
    if (header.name.empty()) {
      ++slot;
      continue;
    }
    if (!isValidHttpHeader(header.name, header.value)) {
      ++slot;
      continue;
    }
    headers[slot++] = std::move(header);
  }
}

void writeProfile(JsonObject obj, const KOReaderSyncProfile& profile) {
  obj["name"] = profile.name;
  obj["username"] = profile.username;
  obj["password_obf"] = obfuscation::obfuscateToBase64(profile.password);
  obj["serverUrl"] = profile.serverUrl;
  writeHeaders(obj, profile.customHeaders);
}

bool readProfile(JsonObjectConst obj, KOReaderSyncProfile& profile, bool* needsResave) {
  profile.name = obj["name"] | std::string("");
  profile.username = obj["username"] | std::string("");
  bool ok = false;
  profile.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "", &ok);
  if (!ok || profile.password.empty()) {
    profile.password = obj["password"] | std::string("");
    if (!profile.password.empty() && needsResave) {
      *needsResave = true;
    }
  }
  profile.serverUrl = obj["serverUrl"] | std::string("");
  readHeaders(obj, profile.customHeaders);
  return true;
}

}  // namespace

namespace KOReaderJsonIO {

bool save(const KOReaderCredentialStore& store, const char* path) {
  JsonDocument doc;
  const auto& active = store.active();
  doc["username"] = active.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(active.password);
  doc["serverUrl"] = active.serverUrl;
  doc["matchMethod"] = static_cast<uint8_t>(store.matchMethod);
  doc["activeProfile"] = static_cast<uint8_t>(store.activeProfile);
  writeHeaders(doc.as<JsonObject>(), active.customHeaders);

  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (const auto& profile : store.profiles) {
    writeProfile(profiles.add<JsonObject>(), profile);
  }

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool load(KOReaderCredentialStore& store, const char* json, bool* needsResave) {
  if (needsResave) *needsResave = false;
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("KRS", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.profiles = {};
  store.activeProfile = 0;
  store.matchMethod = DocumentMatchMethod::FILENAME;

  uint8_t method = doc["matchMethod"] | static_cast<uint8_t>(0);
  store.setMatchMethod(static_cast<DocumentMatchMethod>(method));

  if (doc["profiles"].is<JsonArrayConst>()) {
    size_t index = 0;
    for (JsonObjectConst obj : doc["profiles"].as<JsonArrayConst>()) {
      if (index >= KOReaderCredentialStore::PROFILE_COUNT) {
        break;
      }
      readProfile(obj, store.profiles[index], needsResave);
      ++index;
    }
    const uint8_t active = doc["activeProfile"] | static_cast<uint8_t>(0);
    if (active < KOReaderCredentialStore::PROFILE_COUNT) {
      store.activeProfile = active;
    }
  } else {
    if (needsResave) {
      *needsResave = true;
    }
    readProfile(doc.as<JsonObjectConst>(), store.profiles[0], needsResave);
  }

  return true;
}

}  // namespace KOReaderJsonIO

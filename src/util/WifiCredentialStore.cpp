#include "WifiCredentialStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>
#include <utility>

#include "CrossPointState.h"
#include "network/background/BackgroundWebServer.h"

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
}  // namespace

std::string wifi_credentials::serialize(const WifiCredentialSnapshot& snapshot, const WifiPasswordCodec& codec) {
  JsonDocument doc;
  doc["lastConnectedSsid"] = snapshot.lastConnectedSsid;

  JsonArray arr = doc["credentials"].to<JsonArray>();
  for (const auto& cred : snapshot.credentials) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = cred.ssid;
    // Integrity fields cover the ENCODED bytes, never the plaintext. password_obf
    // exists so that lifting the SD card does not yield passwords; writing the
    // plaintext length and a CRC-32 of the plaintext alongside it would undo that
    // -- CRC-32 is 32 bits and free to compute, so length + checksum together act
    // as an offline verifier for a wordlist guess. Checksumming the ciphertext
    // detects corruption just as well and leaks nothing.
    const std::string encoded = codec.encode(cred.password);
    obj["password_obf"] = encoded;
    obj["password_len"] = static_cast<uint32_t>(encoded.size());
    obj["password_crc32"] = credential_integrity::crc32(encoded);
  }

  std::string json;
  serializeJson(doc, json);
  return json;
}

bool wifi_credentials::parse(const char* json, const WifiPasswordCodec& codec, WifiCredentialSnapshot& out,
                             bool* needsResave) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("WCS", "JSON parse error: %s", error.c_str());
    return false;
  }

  // Two distinct reasons to rewrite, deliberately NOT merged: a legacy-format
  // entry should be migrated, but a corrupt one must never trigger a rewrite --
  // that would persist the file minus the damaged network, turning one flipped
  // bit into permanent credential loss. If anything was corrupt we leave the
  // file exactly as-is so the entry stays recoverable.
  bool resaveForMigration = false;
  bool sawCorruption = false;
  out.lastConnectedSsid = doc["lastConnectedSsid"] | std::string("");
  out.credentials.clear();
  out.credentials.reserve(WifiCredentialStore::MAX_NETWORKS);

  const JsonArrayConst arr = doc["credentials"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (out.credentials.size() >= WifiCredentialStore::MAX_NETWORKS) {
      LOG_WRN("WCS", "Ignoring credentials past the limit of %zu", WifiCredentialStore::MAX_NETWORKS);
      break;
    }

    WifiCredential cred;
    cred.ssid = obj["ssid"] | std::string("");
    if (cred.ssid.empty()) {
      LOG_ERR("WCS", "Discarding credential with no SSID");
      sawCorruption = true;
      continue;
    }

    // The obfuscated field is validated BEFORE it is decoded: length and checksum
    // now describe the ciphertext, so a damaged entry is rejected without doing
    // decode work on bytes we already know are wrong.
    const std::string obfuscated = obj["password_obf"] | std::string("");

    const JsonVariantConst lengthField = obj["password_len"];
    const bool hasLength = !lengthField.isNull();
    if (hasLength) {
      if (!lengthField.is<uint32_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (invalid length)", cred.ssid.c_str());
        sawCorruption = true;
        continue;
      }
      if (obfuscated.size() != static_cast<size_t>(lengthField.as<uint32_t>())) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (stored %u encoded bytes, found %zu)", cred.ssid.c_str(),
                lengthField.as<uint32_t>(), obfuscated.size());
        sawCorruption = true;
        continue;
      }
    }

    const JsonVariantConst checksumField = obj["password_crc32"];
    if (checksumField.is<uint32_t>()) {
      if (credential_integrity::crc32(obfuscated) != checksumField.as<uint32_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (checksum mismatch)", cred.ssid.c_str());
        sawCorruption = true;
        continue;
      }
    } else if (!checksumField.isNull()) {
      LOG_ERR("WCS", "Discarding corrupted password for %s (invalid checksum)", cred.ssid.c_str());
      sawCorruption = true;
      continue;
    }

    bool decoded = false;
    cred.password = codec.decode(obfuscated.c_str(), &decoded);
    if (!decoded || cred.password.empty()) {
      // Pre-obfuscation file: a plaintext "password" field. Reading it is the
      // migration; the rewrite below is what actually upgrades the file.
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty()) {
        resaveForMigration = true;
      }
    }

    if (cred.password.size() > WifiCredentialStore::MAX_PASSWORD_LENGTH) {
      LOG_ERR("WCS", "Discarding oversized password for %s (%zu bytes)", cred.ssid.c_str(), cred.password.size());
      sawCorruption = true;
      continue;
    }

    // An entry that predates the integrity fields is legacy, not corrupt: it
    // should be migrated, and its absence of a checksum is expected.
    if (!hasLength || obj["password_crc32"].isNull()) {
      resaveForMigration = true;
    }

    out.credentials.push_back(std::move(cred));
  }

  if (needsResave) {
    // Migrate only when the whole file parsed cleanly. Rewriting a file that
    // contained a corrupt entry would persist it minus that network.
    *needsResave = resaveForMigration && !sawCorruption;
  }
  if (sawCorruption) {
    LOG_WRN("WCS", "Credential file left unmodified: at least one entry was unreadable");
  }
  return true;
}

std::string wifi_credentials::describe(const WifiCredentialSnapshot& snapshot) {
  std::string summary = std::to_string(snapshot.credentials.size());
  summary += " network(s)";
  for (const auto& cred : snapshot.credentials) {
    summary += "; ";
    summary += cred.ssid;
    summary += cred.password.empty() ? " [open]" : " [secured]";
    if (!snapshot.lastConnectedSsid.empty() && cred.ssid == snapshot.lastConnectedSsid) {
      summary += " [last]";
    }
  }
  return summary;
}

WifiCredentialSnapshot WifiCredentialStore::snapshot() const {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  return WifiCredentialSnapshot{credentials, lastConnectedSsid};
}

void WifiCredentialStore::adoptSnapshot(WifiCredentialSnapshot&& incoming) {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  credentials = std::move(incoming.credentials);
  lastConnectedSsid = std::move(incoming.lastConnectedSsid);
}

bool WifiCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWifi(*this, WIFI_FILE_JSON);
}

bool WifiCredentialStore::loadFromFile() {
  if (Storage.exists(WIFI_FILE_JSON)) {
    HalFile file;
    if (Storage.openFileForRead("WCS", WIFI_FILE_JSON, file)) {
      bool resave = false;
      const bool result = JsonSettingsIO::loadWifi(*this, file, &resave);
      file.close();
      if (result) {
        LOG_DBG("WCS", "Loaded %s", wifi_credentials::describe(snapshot()).c_str());
        if (resave) {
          LOG_DBG("WCS", "Rewriting credential file in the current format");
          saveToFile();
        }
      }
      return result;
    }
  }

  return false;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) {
    LOG_ERR("WCS", "Rejecting credential with no SSID");
    return false;
  }
  if (password.size() > MAX_PASSWORD_LENGTH) {
    LOG_ERR("WCS", "Rejecting password for %s: %zu bytes exceeds the %zu byte limit", ssid.c_str(), password.size(),
            MAX_PASSWORD_LENGTH);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(credentialsMutex);
    const auto cred = std::find_if(credentials.begin(), credentials.end(),
                                   [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
    if (cred != credentials.end()) {
      cred->password = password;
      LOG_INF("WCS", "Updated credentials for: %s", ssid.c_str());
    } else {
      if (credentials.size() >= MAX_NETWORKS) {
        LOG_WRN("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
        return false;
      }
      credentials.push_back({ssid, password});
      LOG_INF("WCS", "Added credentials for: %s", ssid.c_str());
    }
  }

  const bool saved = saveToFile();
  if (saved) {
    if (APP_STATE.wifiAutoConnectWaitingForNewCredential) {
      APP_STATE.wifiAutoConnectWaitingForNewCredential = false;
      APP_STATE.saveToFile();
    }
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  return saved;
}

bool WifiCredentialStore::removeCredential(const std::string& ssid) {
  {
    std::lock_guard<std::mutex> lock(credentialsMutex);
    const auto cred = std::find_if(credentials.begin(), credentials.end(),
                                   [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
    if (cred == credentials.end()) {
      return false;  // Not found
    }
    credentials.erase(cred);
    LOG_INF("WCS", "Removed credentials for: %s", ssid.c_str());
  }

  const bool saved = saveToFile();
  if (saved) {
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  return saved;
}

std::optional<WifiCredential> WifiCredentialStore::findCredential(const std::string& ssid) const {
  if (ssid.empty()) {
    return std::nullopt;
  }

  std::lock_guard<std::mutex> lock(credentialsMutex);
  const auto cred = std::find_if(credentials.begin(), credentials.end(),
                                 [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return *cred;
  }

  return std::nullopt;
}

std::vector<WifiCredential> WifiCredentialStore::getCredentials() const {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  return credentials;
}

std::vector<WifiCredentialSummary> WifiCredentialStore::getCredentialSummaries() const {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  std::vector<WifiCredentialSummary> summaries;
  summaries.reserve(credentials.size());
  for (const auto& cred : credentials) {
    summaries.push_back({cred.ssid, !cred.password.empty(), cred.ssid == lastConnectedSsid});
  }
  return summaries;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid).has_value(); }

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  lastConnectedSsid = ssid;
}

std::string WifiCredentialStore::getLastConnectedSsid() const {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  return lastConnectedSsid;
}

void WifiCredentialStore::clearLastConnectedSsid() {
  std::lock_guard<std::mutex> lock(credentialsMutex);
  lastConnectedSsid.clear();
}

void WifiCredentialStore::clearAll() {
  {
    std::lock_guard<std::mutex> lock(credentialsMutex);
    credentials.clear();
    lastConnectedSsid.clear();
  }

  if (saveToFile()) {
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  LOG_INF("WCS", "Cleared all WiFi credentials");
}

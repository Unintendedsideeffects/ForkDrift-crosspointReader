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
    obj["password_obf"] = codec.encode(cred.password);
    obj["password_len"] = static_cast<uint32_t>(cred.password.size());
    obj["password_crc32"] = credential_integrity::crc32(cred.password);
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

  bool resave = false;
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
      resave = true;
      continue;
    }

    const JsonVariantConst lengthField = obj["password_len"];
    const bool hasLength = !lengthField.isNull();
    size_t expectedLength = 0;
    if (hasLength) {
      if (!lengthField.is<uint32_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (invalid length)", cred.ssid.c_str());
        resave = true;
        continue;
      }
      expectedLength = lengthField.as<uint32_t>();
      if (expectedLength > WifiCredentialStore::MAX_PASSWORD_LENGTH) {
        LOG_ERR("WCS", "Discarding oversized password for %s (%zu bytes)", cred.ssid.c_str(), expectedLength);
        resave = true;
        continue;
      }
    }

    bool decoded = false;
    cred.password = codec.decode(obj["password_obf"] | "", &decoded);
    if (!decoded || cred.password.empty()) {
      cred.password = obj["password"] | std::string("");
      if (!cred.password.empty()) {
        resave = true;
      }
    }

    if (cred.password.size() > WifiCredentialStore::MAX_PASSWORD_LENGTH) {
      LOG_ERR("WCS", "Discarding oversized password for %s (%zu bytes)", cred.ssid.c_str(), cred.password.size());
      resave = true;
      continue;
    }

    if (hasLength) {
      if (cred.password.size() != expectedLength) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (expected %zu bytes, decoded %zu)", cred.ssid.c_str(),
                expectedLength, cred.password.size());
        resave = true;
        continue;
      }
    } else {
      resave = true;
    }

    const JsonVariantConst checksumField = obj["password_crc32"];
    if (checksumField.is<uint32_t>()) {
      if (credential_integrity::crc32(cred.password) != checksumField.as<uint32_t>()) {
        LOG_ERR("WCS", "Discarding corrupted password for %s (checksum mismatch)", cred.ssid.c_str());
        resave = true;
        continue;
      }
    } else if (checksumField.isNull()) {
      resave = true;
    } else {
      LOG_ERR("WCS", "Discarding corrupted password for %s (invalid checksum)", cred.ssid.c_str());
      resave = true;
      continue;
    }

    out.credentials.push_back(std::move(cred));
  }

  if (needsResave) {
    *needsResave = resave;
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

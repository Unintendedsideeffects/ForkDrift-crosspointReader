#include "WifiCredentialStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include "CrossPointState.h"
#include "network/BackgroundWebServer.h"

// Initialize the static instance
WifiCredentialStore WifiCredentialStore::instance;

namespace {
constexpr char WIFI_FILE_JSON[] = "/.crosspoint/wifi.json";
}  // namespace

bool WifiCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveWifi(*this, WIFI_FILE_JSON);
}

bool WifiCredentialStore::loadFromFile() {
  if (Storage.exists(WIFI_FILE_JSON)) {
    FsFile file;
    if (Storage.openFileForRead("WCS", WIFI_FILE_JSON, file)) {
      bool resave = false;
      const bool result = JsonSettingsIO::loadWifi(*this, file, &resave);
      file.close();
      if (result && resave) {
        LOG_DBG("WCS", "Resaving JSON with obfuscated passwords");
        saveToFile();
      }
      return result;
    }
  }

  return false;
}

bool WifiCredentialStore::addCredential(const std::string& ssid, const std::string& password) {
  // Check if this SSID already exists and update it
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    cred->password = password;
    LOG_INF("WCS", "Updated credentials for: %s", ssid.c_str());
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

  // Check if we've reached the limit
  if (credentials.size() >= MAX_NETWORKS) {
    LOG_WRN("WCS", "Cannot add more networks, limit of %zu reached", MAX_NETWORKS);
    return false;
  }

  // Add new credential
  credentials.push_back({ssid, password});
  LOG_INF("WCS", "Added credentials for: %s", ssid.c_str());
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
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });
  if (cred != credentials.end()) {
    credentials.erase(cred);
    LOG_INF("WCS", "Removed credentials for: %s", ssid.c_str());
    const bool saved = saveToFile();
    if (saved) {
      BackgroundWebServer::getInstance().invalidateCredentialsCache();
    }
    return saved;
  }
  return false;  // Not found
}

const WifiCredential* WifiCredentialStore::findCredential(const std::string& ssid) const {
  const auto cred = find_if(credentials.begin(), credentials.end(),
                            [&ssid](const WifiCredential& cred) { return cred.ssid == ssid; });

  if (cred != credentials.end()) {
    return &*cred;
  }

  return nullptr;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const { return findCredential(ssid) != nullptr; }

void WifiCredentialStore::setLastConnectedSsid(const std::string& ssid) { lastConnectedSsid = ssid; }

const std::string& WifiCredentialStore::getLastConnectedSsid() const { return lastConnectedSsid; }

void WifiCredentialStore::clearLastConnectedSsid() { lastConnectedSsid.clear(); }

void WifiCredentialStore::clearAll() {
  credentials.clear();
  lastConnectedSsid.clear();
  if (saveToFile()) {
    BackgroundWebServer::getInstance().invalidateCredentialsCache();
  }
  LOG_INF("WCS", "Cleared all WiFi credentials");
}

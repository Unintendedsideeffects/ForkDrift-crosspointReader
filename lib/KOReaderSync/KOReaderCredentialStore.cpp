#include "KOReaderCredentialStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include "KOReaderJsonIO.h"

#if !CROSSPOINT_HOST_BUILD
#include <MD5Builder.h>
#endif

KOReaderCredentialStore KOReaderCredentialStore::instance;

namespace {
constexpr char KOREADER_FILE_JSON[] = "/.crosspoint/koreader.json";
constexpr char DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";
}  // namespace

bool KOReaderCredentialStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return KOReaderJsonIO::save(*this, KOREADER_FILE_JSON);
}

bool KOReaderCredentialStore::loadFromFile() {
  if (Storage.exists(KOREADER_FILE_JSON)) {
    String json = Storage.readFile(KOREADER_FILE_JSON);
    if (!json.isEmpty()) {
      bool resave = false;
      bool result = KOReaderJsonIO::load(*this, json.c_str(), &resave);
      if (result && resave) {
        if (saveToFile()) {
          LOG_DBG("KRS", "Resaved KOReader credentials to update format");
        } else {
          LOG_ERR("KRS", "Failed to resave KOReader credentials");
        }
      }
      return result;
    }
  }

  LOG_DBG("KRS", "No credentials file found");
  return false;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  active().username = user;
  active().password = pass;
  LOG_DBG("KRS", "Set credentials for user: %s", user.c_str());
}

std::string KOReaderCredentialStore::getMd5Password() const {
  const std::string& password = active().password;
  if (password.empty()) {
    return "";
  }

#if CROSSPOINT_HOST_BUILD
  return password;
#else
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();
  return md5.toString().c_str();
#endif
}

bool KOReaderCredentialStore::hasCredentials() const {
  return !active().username.empty() && !active().password.empty();
}

void KOReaderCredentialStore::clearCredentials() {
  active().username.clear();
  active().password.clear();
  saveToFile();
  LOG_DBG("KRS", "Cleared KOReader credentials");
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  active().serverUrl = url;
  LOG_DBG("KRS", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  std::string url;
  if (active().serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (active().serverUrl.find("://") == std::string::npos) {
    url = "http://" + active().serverUrl;
  } else {
    url = active().serverUrl;
  }

  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  matchMethod = method;
  LOG_DBG("KRS", "Set match method: %s", method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}

bool KOReaderCredentialStore::setActiveProfileIndex(size_t index) {
  if (index >= MAX_PROFILES) {
    return false;
  }
  activeProfile = index;
  return true;
}

void KOReaderCredentialStore::setProfileName(const std::string& name) { active().name = name; }

bool KOReaderCredentialStore::setCustomHeader(size_t slot, const std::string& name, const std::string& value) {
  if (slot >= MAX_CUSTOM_HEADERS) {
    return false;
  }
  if (!isValidHttpHeader(name, value)) {
    return false;
  }
  active().customHeaders[slot].name = name;
  active().customHeaders[slot].value = value;
  return true;
}

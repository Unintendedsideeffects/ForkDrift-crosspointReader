#pragma once

#include <HttpHeader.h>

#include <array>
#include <cstdint>
#include <string>

enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,
  BINARY = 1,
};

struct KOReaderSyncProfile {
  std::string name;
  std::string username;
  std::string password;
  std::string serverUrl;
  std::array<HttpHeader, 2> customHeaders{};
};

class KOReaderCredentialStore;

namespace KOReaderJsonIO {
bool save(const KOReaderCredentialStore& store, const char* path);
bool load(KOReaderCredentialStore& store, const char* json, bool* needsResave);
}  // namespace KOReaderJsonIO

class KOReaderCredentialStore {
 private:
  static KOReaderCredentialStore instance;
  static constexpr size_t MAX_PROFILES = 4;

  std::array<KOReaderSyncProfile, MAX_PROFILES> profiles{};
  size_t activeProfile = 0;
  DocumentMatchMethod matchMethod = DocumentMatchMethod::FILENAME;

  KOReaderCredentialStore() = default;

  friend bool KOReaderJsonIO::save(const KOReaderCredentialStore&, const char*);
  friend bool KOReaderJsonIO::load(KOReaderCredentialStore&, const char*, bool*);

  KOReaderSyncProfile& active() { return profiles[activeProfile]; }
  const KOReaderSyncProfile& active() const { return profiles[activeProfile]; }

 public:
  static constexpr size_t MAX_CUSTOM_HEADERS = 2;
  static constexpr size_t PROFILE_COUNT = MAX_PROFILES;

  KOReaderCredentialStore(const KOReaderCredentialStore&) = delete;
  KOReaderCredentialStore& operator=(const KOReaderCredentialStore&) = delete;

  static KOReaderCredentialStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  void setCredentials(const std::string& user, const std::string& pass);
  const std::string& getUsername() const { return active().username; }
  const std::string& getPassword() const { return active().password; }

  std::string getMd5Password() const;

  bool hasCredentials() const;
  void clearCredentials();

  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const { return active().serverUrl; }
  std::string getBaseUrl() const;

  void setMatchMethod(DocumentMatchMethod method);
  DocumentMatchMethod getMatchMethod() const { return matchMethod; }

  size_t getActiveProfileIndex() const { return activeProfile; }
  bool setActiveProfileIndex(size_t index);

  void setProfileName(const std::string& name);
  const std::string& getProfileName() const { return active().name; }
  const std::array<KOReaderSyncProfile, MAX_PROFILES>& getProfiles() const { return profiles; }

  bool setCustomHeader(size_t slot, const std::string& name, const std::string& value);
  const std::array<HttpHeader, MAX_CUSTOM_HEADERS>& getCustomHeaders() const { return active().customHeaders; }
};

#define KOREADER_STORE KOReaderCredentialStore::getInstance()

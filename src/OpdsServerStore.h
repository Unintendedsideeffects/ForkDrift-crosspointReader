#pragma once

#include <HalStorage.h>
#include <HttpHeader.h>

#include <array>
#include <string>
#include <vector>

struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;
  static constexpr size_t MAX_CUSTOM_HEADERS = 2;
  std::array<HttpHeader, MAX_CUSTOM_HEADERS> customHeaders{};
  std::string downloadFolder;
};

class OpdsServerStore;
namespace JsonSettingsIO {
bool saveOpds(const OpdsServerStore& store, const char* path);
bool loadOpds(OpdsServerStore& store, const char* json, bool* needsResave);
bool loadOpds(OpdsServerStore& store, HalFile& file, bool* needsResave);
}  // namespace JsonSettingsIO

class OpdsServerStore {
 private:
  static OpdsServerStore instance;
  std::vector<OpdsServer> servers;
  bool loadedThisBoot_ = false;
  bool dirty_ = true;

  static constexpr size_t MAX_SERVERS = 8;

  OpdsServerStore() = default;

  friend bool JsonSettingsIO::saveOpds(const OpdsServerStore&, const char*);
  friend bool JsonSettingsIO::loadOpds(OpdsServerStore&, const char*, bool*);
  friend bool JsonSettingsIO::loadOpds(OpdsServerStore&, HalFile&, bool*);

 public:
  OpdsServerStore(const OpdsServerStore&) = delete;
  OpdsServerStore& operator=(const OpdsServerStore&) = delete;

  static OpdsServerStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();
  void markDirty();
  void ensureLoaded();

  bool addServer(const OpdsServer& server);
  bool updateServer(size_t index, const OpdsServer& server);
  bool removeServer(size_t index);

  const std::vector<OpdsServer>& getServers() const { return servers; }
  const OpdsServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
  bool hasServers() const { return !servers.empty(); }
};

#define OPDS_STORE OpdsServerStore::getInstance()

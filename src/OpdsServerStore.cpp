#include "OpdsServerStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include "util/OpdsDownloadFolder.h"

OpdsServerStore OpdsServerStore::instance;

namespace {
constexpr char OPDS_FILE_JSON[] = "/.crosspoint/opds.json";

bool prepareServer(OpdsServer& server) {
  std::string folder;
  if (!OpdsDownloadFolder::normalize(server.downloadFolder, folder)) {
    return false;
  }
  server.downloadFolder = std::move(folder);
  return true;
}
}  // namespace

bool OpdsServerStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveOpds(*this, OPDS_FILE_JSON);
}

bool OpdsServerStore::loadFromFile() {
  if (Storage.exists(OPDS_FILE_JSON)) {
    HalFile file;
    if (Storage.openFileForRead("OPS", OPDS_FILE_JSON, file)) {
      bool resave = false;
      const bool result = JsonSettingsIO::loadOpds(*this, file, &resave);
      file.close();
      if (result && resave) {
        LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
        saveToFile();
      }
      if (result) {
        loadedThisBoot_ = true;
        dirty_ = false;
      }
      return result;
    }
  }

  loadedThisBoot_ = true;
  dirty_ = false;
  return false;
}

void OpdsServerStore::markDirty() { dirty_ = true; }

void OpdsServerStore::ensureLoaded() {
  if (!loadedThisBoot_ || dirty_) {
    loadFromFile();
  }
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  OpdsServer copy = server;
  if (!prepareServer(copy)) {
    return false;
  }

  servers.push_back(std::move(copy));
  LOG_DBG("OPS", "Added server: %s", servers.back().name.c_str());
  markDirty();
  if (!saveToFile()) {
    servers.pop_back();
    return false;
  }
  return true;
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (index >= servers.size()) {
    return false;
  }

  OpdsServer copy = server;
  if (!prepareServer(copy)) {
    return false;
  }

  OpdsServer previous = servers[index];
  servers[index] = std::move(copy);
  LOG_DBG("OPS", "Updated server: %s", servers[index].name.c_str());
  markDirty();
  if (!saveToFile()) {
    servers[index] = std::move(previous);
    return false;
  }
  return true;
}

bool OpdsServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  markDirty();
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}

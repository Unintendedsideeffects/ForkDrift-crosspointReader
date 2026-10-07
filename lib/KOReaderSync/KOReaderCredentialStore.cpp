#include "KOReaderCredentialStore.h"

#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

namespace {
// Default sync server URL. crosspoint-sync speaks the full KOSync protocol, so
// pointing at any other kosync server (e.g. https://sync.koreader.rocks:443)
// still works via the custom server URL setting.
constexpr char DEFAULT_SERVER_URL[] = "https://sync.crosspointreader.com";

// Default before config version 2. Configs saved without a version stamp and an
// empty serverUrl were implicitly syncing here — they get pinned on upgrade.
constexpr char LEGACY_DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";

// Bumped when a change to defaults would alter behavior for existing configs.
// v3: stats/clippings sync flags became explicit choices over a server-based default.
constexpr uint8_t CONFIG_VERSION = 3;
}  // namespace

void KOReaderCredentialStore::toJson(JsonDocument& doc) const {
  doc["cfgVersion"] = CONFIG_VERSION;
  // Serialize fields directly: public getters lazy-load and saveToFile() already
  // holds the store mutex. Calling a getter here could recursively load while locked.
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);
  doc["serverUrl"] = serverUrl;
  doc["matchMethod"] = static_cast<uint8_t>(matchMethod);
  doc["sendMetadata"] = sendMetadata;
  // Three-way choices use their own keys; the legacy booleans are still written
  // so older firmware keeps an explicit ON. Older firmware saves "never chosen"
  // and "chosen off" as the same false, so an OFF cannot survive its rewrite.
  doc["statsChoice"] = static_cast<uint8_t>(statsChoice);
  doc["clippingsChoice"] = static_cast<uint8_t>(clippingsChoice);
  doc["syncStats"] = statsChoice == IncludeChoice::ON;
  doc["syncClippings"] = clippingsChoice == IncludeChoice::ON;
  if (serverSupport != SyncServerSupport::UNKNOWN && !serverSupportUrl.empty()) {
    doc["serverSupport"] = static_cast<uint8_t>(serverSupport);
    doc["serverSupportUrl"] = serverSupportUrl;
  }
  doc["syncBehavior"] = static_cast<uint8_t>(syncBehavior);
}

bool KOReaderCredentialStore::fromJson(JsonVariantConst doc) {
  std::string user = doc["username"] | "";

  bool needsResave = false;
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::LEGACY && !pass.empty()) {
    needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY || pass.empty()) {
    pass = doc["password"] | "";
    if (!pass.empty()) needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID && pass.empty()) {
    LOG_ERR("KRS", "Ignoring unreadable KOReader password");
  }

  setCredentials(user, pass);
  setServerUrl(doc["serverUrl"] | "");

  // The default server changed in config v2 (sync.koreader.rocks -> crosspoint-sync).
  // A pre-v2 config with credentials and no explicit URL was actively syncing
  // against the old default — pin that URL so the upgrade doesn't switch servers
  // out from under the user. Fresh setups get the new default.
  const uint8_t cfgVersion = doc["cfgVersion"] | (uint8_t)1;
  if (cfgVersion < 2 && getServerUrl().empty() && hasCredentials()) {
    LOG_DBG("KRS", "Pre-v2 config used the old default server; pinning %s", LEGACY_DEFAULT_SERVER_URL);
    setServerUrl(LEGACY_DEFAULT_SERVER_URL);
  }
  if (cfgVersion < CONFIG_VERSION) needsResave = true;  // stamp cfgVersion so migrations run once

  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  if (method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)) {
    setMatchMethod(static_cast<DocumentMatchMethod>(method));
  } else {
    LOG_DBG("KRS", "Invalid matchMethod %u in JSON, resetting to FILENAME", method);
    setMatchMethod(DocumentMatchMethod::FILENAME);
  }
  setSendMetadata(doc["sendMetadata"] | false);
  // Legacy booleans were always written and defaulted to false, so a saved false
  // was never a user choice: only a saved true migrates to an explicit ON.
  const auto loadChoice = [](JsonVariantConst choice, JsonVariantConst legacy) {
    const uint8_t value = choice | static_cast<uint8_t>(0xFF);
    if (value <= static_cast<uint8_t>(IncludeChoice::ON)) return static_cast<IncludeChoice>(value);
    return (legacy | false) ? IncludeChoice::ON : IncludeChoice::AUTO;
  };
  const bool missingChoices = doc["statsChoice"].isNull() || doc["clippingsChoice"].isNull();
  statsChoice = loadChoice(doc["statsChoice"], doc["syncStats"]);
  clippingsChoice = loadChoice(doc["clippingsChoice"], doc["syncClippings"]);
  needsResave = needsResave || missingChoices;
  const uint8_t support = doc["serverSupport"] | (uint8_t)0;
  serverSupportUrl = doc["serverSupportUrl"] | "";
  serverSupport = support <= static_cast<uint8_t>(SyncServerSupport::UNSUPPORTED)
                      ? static_cast<SyncServerSupport>(support)
                      : SyncServerSupport::UNKNOWN;

  const JsonVariantConst behaviorValue = doc["syncBehavior"];
  const bool missingBehavior = behaviorValue.isNull();
  uint8_t behavior = behaviorValue | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
  if (behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    setSyncBehavior(static_cast<KOReaderSyncBehavior>(behavior));
    needsResave = needsResave || missingBehavior;
  } else {
    LOG_DBG("KRS", "Invalid syncBehavior %u in JSON, resetting to ASK_EVERY_TIME", behavior);
    setSyncBehavior(KOReaderSyncBehavior::ASK_EVERY_TIME);
    needsResave = true;
  }

  if (needsResave) {
    LOG_DBG("KRS", "Resaving KOReader credentials to update format");
    requestResave();
  }

  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  ensureLoaded();
  username = user;
  password = pass;
}

std::string KOReaderCredentialStore::getMd5Password() const {
  ensureLoaded();
  if (password.empty()) {
    return "";
  }

  // Calculate MD5 hash of password using ESP32's MD5Builder
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const {
  ensureLoaded();
  return !username.empty() && !password.empty();
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  ensureLoaded();
  serverUrl = url;
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  ensureLoaded();
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

bool KOReaderCredentialStore::usesCrossPointSyncServer() const { return getBaseUrl() == DEFAULT_SERVER_URL; }

SyncServerSupport KOReaderCredentialStore::getServerSupport() const {
  ensureLoaded();
  const std::string base = getBaseUrl();
  if (base == DEFAULT_SERVER_URL) return SyncServerSupport::SUPPORTED;
  // A changed URL (from the device or the web portal) invalidates what was learned.
  return base == serverSupportUrl ? serverSupport : SyncServerSupport::UNKNOWN;
}

void KOReaderCredentialStore::setServerSupport(const SyncServerSupport support) {
  ensureLoaded();
  std::string base = getBaseUrl();
  if (base == DEFAULT_SERVER_URL) return;  // Always supported; nothing to learn or persist.
  if (support == serverSupport && base == serverSupportUrl) return;
  serverSupport = support;
  serverSupportUrl = std::move(base);
  LOG_INF("KRS", "Sync server extensions: %s",
          support == SyncServerSupport::SUPPORTED     ? "supported"
          : support == SyncServerSupport::UNSUPPORTED ? "unsupported"
                                                      : "unknown");
  if (!saveToFile()) LOG_ERR("KRS", "Cannot save sync server support");
}

bool KOReaderCredentialStore::needsServerProbe() const {
  ensureLoaded();
  // Explicit choices already trigger an upload that teaches us the answer.
  return getServerSupport() == SyncServerSupport::UNKNOWN &&
         (statsChoice == IncludeChoice::AUTO || clippingsChoice == IncludeChoice::AUTO) && probedUrl != getBaseUrl();
}

void KOReaderCredentialStore::markServerProbed() const {
  ensureLoaded();
  probedUrl = getBaseUrl();
}

bool KOReaderCredentialStore::getSyncStats() const {
  ensureLoaded();
  const auto support = getServerSupport();
  if (support == SyncServerSupport::UNSUPPORTED) return false;
  return statsChoice == IncludeChoice::ON ||
         (statsChoice == IncludeChoice::AUTO && support == SyncServerSupport::SUPPORTED);
}

bool KOReaderCredentialStore::getSyncClippings() const {
  ensureLoaded();
  const auto support = getServerSupport();
  if (support == SyncServerSupport::UNSUPPORTED) return false;
  return clippingsChoice == IncludeChoice::ON ||
         (clippingsChoice == IncludeChoice::AUTO && support == SyncServerSupport::SUPPORTED);
}

void KOReaderCredentialStore::setSyncStats(const bool enabled) {
  ensureLoaded();
  statsChoice = enabled ? IncludeChoice::ON : IncludeChoice::OFF;
}

void KOReaderCredentialStore::setSyncClippings(const bool enabled) {
  ensureLoaded();
  clippingsChoice = enabled ? IncludeChoice::ON : IncludeChoice::OFF;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  ensureLoaded();
  matchMethod = method;
}

void KOReaderCredentialStore::setSendMetadata(bool enabled) {
  ensureLoaded();
  sendMetadata = enabled;
}

void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior behavior) {
  ensureLoaded();
  if (static_cast<uint8_t>(behavior) > static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    behavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
  }
  syncBehavior = behavior;
}

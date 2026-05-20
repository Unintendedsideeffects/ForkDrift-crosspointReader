#include "core/features/FeatureModules.h"

#include <ArduinoJson.h>
#include <FeatureFlags.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <utility>

#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif

// Standard thumbnail height for cover images (matches HomeActivity: screenHeight/2 on 480px display).
// This height is used wherever a cover BMP is referenced outside of a specific render context.
static constexpr int kDefaultThumbHeight = 240;

#include "CrossPointSettings.h"
#include "SpiBusMutex.h"
#include "activities/settings/SettingsActivity.h"
#include "core/features/FeatureCatalog.h"
#include "core/registries/LifecycleRegistry.h"
#include "core/registries/ReaderRegistry.h"
#include "core/registries/SettingsActionRegistry.h"
#include "core/registries/SyncServiceRegistry.h"
#include "util/StringUtils.h"
#if ENABLE_XTC_SUPPORT
#include "Xtc.h"
#endif

#if ENABLE_INTEGRATIONS && ENABLE_KOREADER_SYNC
#include "KOReaderCredentialStore.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#endif

#if ENABLE_TODO_PLANNER
#include "activities/todo/TodoActivity.h"
#include "activities/todo/TodoFallbackActivity.h"
#endif

namespace core {

namespace {
constexpr size_t toIndex(const Capability capability) { return static_cast<size_t>(capability); }

constexpr std::array<const char*, toIndex(Capability::Count)> kCapabilityFeatureKeys = {{
    "anki_support",
    nullptr,
    "background_server_always",
    nullptr,
    "ble_wifi_provisioning",
    "calibre_sync",
    "dark_mode",
    "epub_support",
    "global_status_bar",
    "home_media_picker",
    "koreader_sync",
    "lyra_theme",
    "markdown",
    "ota_updates",
    "pokemon_party",
    "pokemon_wallpaper_plugin",
    "remote_keyboard_input",
    nullptr,
    nullptr,
    "todo_planner",
    "trmnl_switch",
    "usb_mass_storage",
    "user_fonts",
    "visual_cover_picker",
    "web_wallpaper_plugin",
    "web_wifi_setup",
    "xtc_support",
}};

bool isEpubDocumentPath(const std::string& path) { return FsHelpers::checkFileExtension(path, ".epub"); }

bool isXtcDocumentPath(const std::string& path) {
  return FsHelpers::checkFileExtension(path, ".xtc") || FsHelpers::checkFileExtension(path, ".xtch");
}

bool isMarkdownDocumentPath(const std::string& path) { return FsHelpers::checkFileExtension(path, ".md"); }

std::string fileNameFromPath(const std::string& path) {
  const size_t lastSlash = path.find_last_of('/');
  if (lastSlash == std::string::npos) {
    return path;
  }
  return path.substr(lastSlash + 1);
}

}  // namespace

bool FeatureModules::isEnabled(const char* featureKey) { return FeatureCatalog::isEnabled(featureKey); }

bool FeatureModules::hasCapability(const Capability capability) {
  if (capability == Capability::BackgroundServer) {
    return isEnabled("background_server") || hasCapability(Capability::BackgroundServerOnCharge);
  }

  if (capability == Capability::BackgroundServerOnCharge) {
    return isEnabled("background_server_on_charge") || hasCapability(Capability::BackgroundServerAlways);
  }

  if (capability == Capability::RemoteOpenBook || capability == Capability::RemotePageTurn) {
    return true;
  }

  const char* const featureKey = kCapabilityFeatureKeys[toIndex(capability)];
  if (featureKey == nullptr) {
    return false;
  }
  return isEnabled(featureKey);
}

String FeatureModules::getBuildString() { return FeatureCatalog::buildString(); }

String FeatureModules::getFeatureMapJson() {
  JsonDocument doc;
  deserializeJson(doc, FeatureCatalog::toJson());
  doc["remote_keyboard_input"] = hasCapability(Capability::RemoteKeyboardInput);
  doc["remote_open_book"] = hasCapability(Capability::RemoteOpenBook);
  doc["remote_page_turn"] = hasCapability(Capability::RemotePageTurn);

  String json;
  serializeJson(doc, json);
  return json;
}

FeatureModules::HomeCardDataResult FeatureModules::resolveHomeCardData(const std::string& path, const int thumbHeight) {
  HomeCardDataResult result;
  if (path.empty() || thumbHeight <= 0) {
    return result;
  }

  if (isEpubDocumentPath(path)) {
    result.handled = true;
    if (!hasCapability(Capability::EpubSupport)) {
      return result;
    }
#if ENABLE_EPUB_SUPPORT
    Epub epub(path, "/.crosspoint");
    if (!epub.load(false, true)) {
      return result;
    }

    result.loaded = true;
    if (!epub.getTitle().empty()) {
      result.title = epub.getTitle();
    }
    if (!epub.getAuthor().empty()) {
      result.author = epub.getAuthor();
    }
    if (epub.generateThumbBmp(thumbHeight)) {
      result.coverPath = epub.getThumbBmpPath(thumbHeight);
    }
#endif
    return result;
  }

  if (isXtcDocumentPath(path)) {
    result.handled = true;
    if (!hasCapability(Capability::XtcSupport)) {
      return result;
    }
#if ENABLE_XTC_SUPPORT
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) {
      return result;
    }

    result.loaded = true;
    const std::string xtcTitle = xtc.getTitle();
    if (!xtcTitle.empty()) {
      result.title = xtcTitle;
    }
    const std::string xtcAuthor = xtc.getAuthor();
    if (!xtcAuthor.empty()) {
      result.author = xtcAuthor;
    }
    if (xtc.generateThumbBmp(thumbHeight)) {
      result.coverPath = xtc.getThumbBmpPath(thumbHeight);
    }
#endif
    return result;
  }

  return result;
}

FeatureModules::RecentBookDataResult FeatureModules::resolveRecentBookData(const std::string& path) {
  RecentBookDataResult result;
  if (path.empty()) {
    return result;
  }

  if (isEpubDocumentPath(path)) {
    result.handled = true;
    if (!hasCapability(Capability::EpubSupport)) {
      return result;
    }
#if ENABLE_EPUB_SUPPORT
    Epub epub(path, "/.crosspoint");
    // Match resolveHomeCardData behavior: only expose metadata when the file loaded.
    if (!epub.load(false)) {
      return result;
    }
    result.title = epub.getTitle();
    result.author = epub.getAuthor();
    result.coverPath = epub.getThumbBmpPath(kDefaultThumbHeight);
#endif
    return result;
  }

  if (isXtcDocumentPath(path)) {
    result.handled = true;
    if (!hasCapability(Capability::XtcSupport)) {
      return result;
    }
#if ENABLE_XTC_SUPPORT
    Xtc xtc(path, "/.crosspoint");
    if (!xtc.load()) {
      return result;
    }

    result.title = xtc.getTitle();
    result.author = xtc.getAuthor();
    result.coverPath = xtc.getThumbBmpPath(kDefaultThumbHeight);
#endif
    return result;
  }

  if (FsHelpers::checkFileExtension(path, ".txt") || isMarkdownDocumentPath(path)) {
    result.handled = true;
    result.title = fileNameFromPath(path);
    return result;
  }

  return result;
}

bool FeatureModules::isSupportedLibraryFile(const std::string& path) {
  return ReaderRegistry::isSupportedLibraryFile(path);
}

bool FeatureModules::supportsSettingAction(const SettingAction action) {
  switch (action) {
    case SettingAction::RemapFrontButtons:
    case SettingAction::Network:
    case SettingAction::ClearCache:
    case SettingAction::FactoryReset:
    case SettingAction::ValidateSleepImages:
    case SettingAction::SdFirmwareUpdate:
      return true;
    case SettingAction::KOReaderSync:
    case SettingAction::OPDSBrowser:
    case SettingAction::CheckForUpdates:
      return core::SettingsActionRegistry::isSupported(action);
    case SettingAction::PokemonParty:
      return hasCapability(Capability::PokemonParty);
    case SettingAction::SwitchToTrmnl:
      return hasCapability(Capability::TrmnlSwitch);
    case SettingAction::Language:
      return true;
    case SettingAction::None:
      return false;
  }
  return false;
}

bool FeatureModules::hasKoreaderSyncCredentials() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api == nullptr || api->hasCredentials == nullptr) {
    return false;
  }
  return api->hasCredentials();
}

Activity* FeatureModules::createTodoPlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                    std::string filePath, std::string dateTitle, void* onBackCtx,
                                                    void (*onBack)(void*)) {
#if ENABLE_TODO_PLANNER
  if (!hasCapability(Capability::TodoPlanner)) {
    return nullptr;
  }
  return new TodoActivity(renderer, mappedInput, std::move(filePath), std::move(dateTitle), onBackCtx, onBack);
#else
  (void)renderer;
  (void)mappedInput;
  (void)filePath;
  (void)dateTitle;
  (void)onBackCtx;
  (void)onBack;
  return nullptr;
#endif
}

Activity* FeatureModules::createTodoFallbackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     std::string dateText, void* onBackCtx, void (*onBack)(void*)) {
#if ENABLE_TODO_PLANNER
  if (!hasCapability(Capability::TodoPlanner)) {
    return nullptr;
  }
  return new TodoFallbackActivity(renderer, mappedInput, std::move(dateText), onBackCtx, onBack);
#else
  (void)renderer;
  (void)mappedInput;
  (void)dateText;
  (void)onBackCtx;
  (void)onBack;
  return nullptr;
#endif
}

std::string FeatureModules::getKoreaderUsername() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api == nullptr || api->getUsername == nullptr) {
    return "";
  }

  char buffer[kSyncCredBufSize] = {};
  api->getUsername(buffer, sizeof(buffer));
  return buffer;
}

std::string FeatureModules::getKoreaderPassword() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api == nullptr || api->getPassword == nullptr) {
    return "";
  }
  char buffer[kSyncCredBufSize] = {};
  api->getPassword(buffer, sizeof(buffer));
  return buffer;
}

std::string FeatureModules::getKoreaderServerUrl() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api == nullptr || api->getServerUrl == nullptr) {
    return "";
  }

  char buffer[kSyncUrlBufSize] = {};
  api->getServerUrl(buffer, sizeof(buffer));
  return buffer;
}

uint8_t FeatureModules::getKoreaderMatchMethod() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api == nullptr || api->getMatchMethod == nullptr) {
    return 0;
  }
  return api->getMatchMethod();
}

void FeatureModules::setKoreaderUsername(const std::string& username, const bool save) {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api != nullptr && api->setUsername != nullptr) {
    api->setUsername(username.c_str(), save);
  }
}

void FeatureModules::setKoreaderPassword(const std::string& password, const bool save) {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api != nullptr && api->setPassword != nullptr) {
    api->setPassword(password.c_str(), save);
  }
}

void FeatureModules::setKoreaderServerUrl(const std::string& serverUrl, const bool save) {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api != nullptr && api->setServerUrl != nullptr) {
    api->setServerUrl(serverUrl.c_str(), save);
  }
}

void FeatureModules::setKoreaderMatchMethod(const uint8_t method, const bool save) {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api != nullptr && api->setMatchMethod != nullptr) {
    api->setMatchMethod(method, save);
  }
}

void FeatureModules::saveKoreaderSettings() {
  const auto* api = SyncServiceRegistry::getKoreaderApi();
  if (api != nullptr && api->saveSettings != nullptr) {
    api->saveSettings();
  }
}

std::vector<std::string> FeatureModules::getUserFontFamilies() {
  return {};
}

uint8_t FeatureModules::getSelectedUserFontFamilyIndex() {
  return 0;
}

void FeatureModules::setSelectedUserFontFamilyIndex(const uint8_t index) {
  (void)index;
}

void FeatureModules::onFontFamilySettingChanged(const uint8_t newValue) {
  LifecycleRegistry::dispatchFontFamilyChanged(newValue);
}

void FeatureModules::onWebSettingsApplied() { LifecycleRegistry::dispatchWebSettingsApplied(); }

void FeatureModules::onUploadCompleted(const String& uploadPath, const String& uploadFileName) {
  LifecycleRegistry::dispatchUploadCompleted(uploadPath.c_str(), uploadFileName.c_str());
}

void FeatureModules::onWebFileChanged(const String& filePath) {
#if ENABLE_EPUB_SUPPORT
  if (FsHelpers::checkFileExtension(filePath, ".epub")) {
    Epub(filePath.c_str(), "/.crosspoint").clearCache();
    LOG_DBG("FEATURES", "Cleared epub cache for: %s", filePath.c_str());
  }
#else
  (void)filePath;
#endif
}

bool FeatureModules::tryGetDocumentCoverPath(const String& documentPath, std::string& outCoverPath) {
#if ENABLE_EPUB_SUPPORT
  String lowerPath = documentPath;
  lowerPath.toLowerCase();
  if (!lowerPath.endsWith(".epub")) {
    return false;
  }

  Epub epub(documentPath.c_str(), "/.crosspoint");
  SpiBusMutex::Guard guard;
  if (!epub.load(false)) {
    return false;
  }

  outCoverPath = epub.getThumbBmpPath(kDefaultThumbHeight);
  return !outCoverPath.empty();
#else
  (void)documentPath;
  outCoverPath.clear();
  return false;
#endif
}

}  // namespace core

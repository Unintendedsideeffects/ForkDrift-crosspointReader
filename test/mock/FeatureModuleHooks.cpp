#include "core/features/FeatureModules.h"

#include "include/FeatureFlags.h"

namespace core {

bool FeatureModules::isEnabled(const char*) { return false; }

bool FeatureModules::hasCapability(const Capability capability) {
  switch (capability) {
    case Capability::BackgroundServer:
#if ENABLE_BACKGROUND_SERVER || ENABLE_BACKGROUND_SERVER_ON_CHARGE || ENABLE_BACKGROUND_SERVER_ALWAYS
      return true;
#else
      return false;
#endif
    case Capability::BackgroundServerAlways:
#if ENABLE_BACKGROUND_SERVER_ALWAYS
      return true;
#else
      return false;
#endif
    case Capability::BackgroundServerOnCharge:
#if ENABLE_BACKGROUND_SERVER_ON_CHARGE || ENABLE_BACKGROUND_SERVER_ALWAYS
      return true;
#else
      return false;
#endif
    case Capability::CalibreSync:
#if ENABLE_CALIBRE_SYNC
      return true;
#else
      return false;
#endif
    case Capability::DarkMode:
#if ENABLE_DARK_MODE
      return true;
#else
      return false;
#endif
    case Capability::FocusReading:
#if ENABLE_FOCUS_READING
      return true;
#else
      return false;
#endif
    case Capability::GlobalStatusBar:
#if ENABLE_GLOBAL_STATUS_BAR
      return true;
#else
      return false;
#endif
    case Capability::KoreaderSync:
#if ENABLE_KOREADER_SYNC
      return true;
#else
      return false;
#endif
    case Capability::LyraTheme:
#if ENABLE_LYRA_THEME
      return true;
#else
      return false;
#endif
    case Capability::MinimalTheme:
#if ENABLE_MINIMAL_THEME
      return true;
#else
      return false;
#endif
    case Capability::PokemonParty:
#if ENABLE_POKEMON_PARTY
      return true;
#else
      return false;
#endif
    case Capability::TrmnlSwitch:
#if ENABLE_TRMNL_SWITCH
      return true;
#else
      return false;
#endif
    case Capability::UsbMassStorage:
#if ENABLE_USB_MASS_STORAGE
      return true;
#else
      return false;
#endif
    case Capability::UserFonts:
#if ENABLE_USER_FONTS
      return true;
#else
      return false;
#endif
    default:
      return false;
  }
}

String FeatureModules::getBuildString() { return ""; }

String FeatureModules::getFeatureMapJson() { return "{}"; }

bool FeatureModules::supportsSettingAction(SettingAction) { return false; }

void FeatureModules::onUploadCompleted(const String&, const String&) {}

void FeatureModules::onWebFileChanged(const String&) {}

void FeatureModules::onWebSettingsApplied() {}

bool FeatureModules::hasKoreaderSyncCredentials() { return false; }

std::string FeatureModules::getKoreaderUsername() { return ""; }

std::string FeatureModules::getKoreaderPassword() { return ""; }

std::string FeatureModules::getKoreaderServerUrl() { return ""; }

uint8_t FeatureModules::getKoreaderMatchMethod() { return 0; }

void FeatureModules::setKoreaderUsername(const std::string&, bool) {}

void FeatureModules::setKoreaderPassword(const std::string&, bool) {}

void FeatureModules::setKoreaderServerUrl(const std::string&, bool) {}

void FeatureModules::setKoreaderMatchMethod(uint8_t, bool) {}

void FeatureModules::saveKoreaderSettings() {}

std::vector<std::string> FeatureModules::getUserFontFamilies() { return {}; }

uint8_t FeatureModules::getSelectedUserFontFamilyIndex() { return 0; }

void FeatureModules::setSelectedUserFontFamilyIndex(uint8_t) {}

}  // namespace core

void invalidateSleepImageCache() {}

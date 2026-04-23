#pragma once

#include <Arduino.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Activity;
class CrossPointWebServer;
class Epub;
class GfxRenderer;
class MappedInputManager;
class WebServer;
enum class SettingAction;

namespace core {

enum class Capability {
  AnkiSupport,
  BackgroundServer,
  BackgroundServerAlways,
  BackgroundServerOnCharge,
  BleWifiProvisioning,
  CalibreSync,
  DarkMode,
  EpubSupport,
  GlobalStatusBar,
  HomeMediaPicker,
  KoreaderSync,
  LyraTheme,
  MarkdownSupport,
  OtaUpdates,
  PokemonParty,
  PokemonWallpaperPlugin,
  RemoteKeyboardInput,
  RemoteOpenBook,
  RemotePageTurn,
  TodoPlanner,
  TrmnlSwitch,
  UsbMassStorage,
  UserFonts,
  VisualCoverPicker,
  WebWallpaperPlugin,
  WebWifiSetup,
  XtcSupport,
  Count,
};

class FeatureModules {
 public:
  struct HomeCardDataResult {
    bool handled = false;
    bool loaded = false;
    std::string title;
    std::string author;
    std::string coverPath;
  };

  struct RecentBookDataResult {
    bool handled = false;
    std::string title;
    std::string author;
    std::string coverPath;
  };

  static bool isEnabled(const char* featureKey);
  static bool hasCapability(Capability capability);
  static String getBuildString();
  static String getFeatureMapJson();
  static bool supportsSettingAction(SettingAction action);
  static HomeCardDataResult resolveHomeCardData(const std::string& path, int thumbHeight);
  static RecentBookDataResult resolveRecentBookData(const std::string& path);
  static bool isSupportedLibraryFile(const std::string& path);
  static bool hasKoreaderSyncCredentials();
  static Activity* createTodoPlannerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string filePath, std::string dateTitle, void* onBackCtx,
                                             void (*onBack)(void*));
  static Activity* createTodoFallbackActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              std::string dateText, void* onBackCtx, void (*onBack)(void*));

  static void onFontFamilySettingChanged(uint8_t newValue);
  static void onWebSettingsApplied();
  static void onUploadCompleted(const String& uploadPath, const String& uploadFileName);
  static void onWebFileChanged(const String& filePath);
  static bool tryGetDocumentCoverPath(const String& documentPath, std::string& outCoverPath);

  static std::string getKoreaderUsername();
  static std::string getKoreaderPassword();
  static std::string getKoreaderServerUrl();
  static uint8_t getKoreaderMatchMethod();
  static void setKoreaderUsername(const std::string& username, bool save = true);
  static void setKoreaderPassword(const std::string& password, bool save = true);
  static void setKoreaderServerUrl(const std::string& serverUrl, bool save = true);
  static void setKoreaderMatchMethod(uint8_t method, bool save = true);
  static void saveKoreaderSettings();
  static std::vector<std::string> getUserFontFamilies();
  static uint8_t getSelectedUserFontFamilyIndex();
  static void setSelectedUserFontFamilyIndex(uint8_t index);
};

}  // namespace core

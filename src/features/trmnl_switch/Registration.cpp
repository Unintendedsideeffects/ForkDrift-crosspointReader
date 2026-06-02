#include "features/trmnl_switch/Registration.h"

#include <FeatureFlags.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "core/features/FeatureModules.h"
#include "core/registries/LifecycleRegistry.h"
#include "network/HttpDownloader.h"

namespace features::trmnl_switch {

#if ENABLE_TRMNL_SWITCH
namespace {

static constexpr const char* TRMNL_DEST_PATH = "/sleep/trmnl_latest.bmp";

static void onBackgroundServerStarted() {
  if (!SETTINGS.trmnlSleepEnabled) {
    return;
  }
  if (SETTINGS.trmnlByosUrl[0] == '\0') {
    LOG_INF("TRMNL", "TRMNL sleep enabled but no URL configured");
    return;
  }

  LOG_INF("TRMNL", "Fetching TRMNL image from %s", SETTINGS.trmnlByosUrl);

  const std::string url(SETTINGS.trmnlByosUrl);
  const auto err = HttpDownloader::downloadToFile(url, TRMNL_DEST_PATH);
  if (err != HttpDownloader::OK) {
    LOG_ERR("TRMNL", "Image fetch failed (err=%d), keeping existing pin", static_cast<int>(err));
    return;
  }

  // Pin the image as the next sleep screen.
  strncpy(SETTINGS.sleepPinnedPath, TRMNL_DEST_PATH, sizeof(SETTINGS.sleepPinnedPath) - 1);
  SETTINGS.sleepPinnedPath[sizeof(SETTINGS.sleepPinnedPath) - 1] = '\0';

  // Ensure CUSTOM sleep mode is active so the pin is respected.
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
  }

  SETTINGS.saveToFile();
  LOG_INF("TRMNL", "TRMNL image pinned as next sleep screen");
}

}  // namespace
#endif

void registerFeature() {
#if ENABLE_TRMNL_SWITCH
  if (!core::FeatureModules::hasCapability(core::Capability::TrmnlSwitch)) {
    return;
  }
  core::LifecycleEntry entry{};
  entry.onBackgroundServerStarted = onBackgroundServerStarted;
  core::LifecycleRegistry::add(entry);
#endif
}

}  // namespace features::trmnl_switch

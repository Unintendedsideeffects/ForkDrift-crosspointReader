#include "MaintenanceUtils.h"

#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "SpiBusMutex.h"
#include "util/WifiCredentialStore.h"

namespace MaintenanceUtils {

CacheClearResult clearReadingCache() {
  CacheClearResult result;
  LOG_DBG("MAINT", "Clearing reading cache...");

  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("MAINT", "Failed to open cache directory");
    if (root) {
      root.close();
    }
    result.failed = 1;
    return result;
  }

  char name[128];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    if (file.isDirectory() && (itemName.startsWith("epub_") || itemName.startsWith("xtc_"))) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("MAINT", "Removing cache: %s", fullPath.c_str());

      file.close();

      if (Storage.removeDir(fullPath.c_str())) {
        result.removed++;
      } else {
        LOG_ERR("MAINT", "Failed to remove: %s", fullPath.c_str());
        result.failed++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("MAINT", "Cache cleared: %d removed, %d failed", result.removed, result.failed);
  return result;
}

SleepValidationResult validateSleepImages() {
  const SleepImageValidationStats stats = validateSleepImagesWithStats();
  return {stats.valid, stats.invalid};
}

bool resetSettingsToDefaults() {
  if (!CrossPointSettings::resetToDefaults()) {
    return false;
  }
  LOG_INF("MAINT", "Settings reset to defaults");
  return true;
}

bool clearWifiNetworks() {
  WIFI_STORE.clearAll();
  return true;
}

bool clearLogs() {
  SpiBusMutex::Guard guard;
  Storage.remove("/crosspoint-debug.log");
  clearLastLogs();
  LOG_INF("MAINT", "Logs cleared");
  return true;
}

bool clearCrashReports() {
  SpiBusMutex::Guard guard;
  Storage.remove("/crash_report.txt");
  HalSystem::clearPanic();
  LOG_INF("MAINT", "Crash reports cleared");
  return true;
}

}  // namespace MaintenanceUtils

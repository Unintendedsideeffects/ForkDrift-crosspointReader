#pragma once

namespace MaintenanceUtils {

struct CacheClearResult {
  int removed = 0;
  int failed = 0;
};

struct SleepValidationResult {
  int valid = 0;
  int invalid = 0;
};

CacheClearResult clearReadingCache();
SleepValidationResult validateSleepImages();
bool resetSettingsToDefaults();
bool clearWifiNetworks();
bool clearLogs();
bool clearCrashReports();

}  // namespace MaintenanceUtils

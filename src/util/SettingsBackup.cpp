#include "util/SettingsBackup.h"

#include <HalStorage.h>
#include <Logging.h>

#include "JsonSettingsIO.h"

namespace settings_backup {

bool backup(const CrossPointSettings& s) { return JsonSettingsIO::saveSettings(s, kBackupPath); }

bool hasBackup() { return Storage.exists(kBackupPath); }

bool restore(CrossPointSettings& s) {
  if (!hasBackup()) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("BACKUP", kBackupPath, file)) {
    return false;
  }
  bool needsResave = false;
  const bool result = JsonSettingsIO::loadSettings(s, file, &needsResave);
  file.close();
  if (result) {
    s.validateAndClamp();
    s.saveToFile();
  }
  return result;
}

}  // namespace settings_backup

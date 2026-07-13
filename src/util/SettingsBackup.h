#pragma once

#include "CrossPointSettings.h"

namespace settings_backup {
constexpr char kBackupPath[] = "/settings-backup.json";
bool backup(const CrossPointSettings& s);
bool hasBackup();
bool restore(CrossPointSettings& s);
}  // namespace settings_backup

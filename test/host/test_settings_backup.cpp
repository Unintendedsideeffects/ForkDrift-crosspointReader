#include "doctest/doctest.h"
#include "src/CrossPointSettings.h"
#include "src/util/SettingsBackup.h"
#include "test/mock/HalStorage.h"

TEST_CASE("SettingsBackup tests") {
  Storage.reset();

  CrossPointSettings& s = CrossPointSettings::getInstance();

  SUBCASE("missing-file restore returns false") {
    if (Storage.exists(settings_backup::kBackupPath)) {
      Storage.remove(settings_backup::kBackupPath);
    }
    CHECK_FALSE(settings_backup::hasBackup());
    CHECK_FALSE(settings_backup::restore(s));
  }

  SUBCASE("round-trip and backup overwrites") {
    // Set settings to known values
    s.sleepScreen = CrossPointSettings::LIGHT;
    s.fontSize = CrossPointSettings::LARGE;
    s.cleanSleepRefresh = 1;

    // Perform backup
    CHECK(settings_backup::backup(s));
    CHECK(settings_backup::hasBackup());

    // Mutate the settings to different values
    s.sleepScreen = CrossPointSettings::DARK;
    s.fontSize = CrossPointSettings::SMALL;
    s.cleanSleepRefresh = 0;

    // Restore settings
    CHECK(settings_backup::restore(s));

    // Verify values match the backup
    CHECK(s.sleepScreen == CrossPointSettings::LIGHT);
    CHECK(s.fontSize == CrossPointSettings::LARGE);
    CHECK(s.cleanSleepRefresh == 1);

    // Mutate again and verify backup overwrites previous backup
    s.sleepScreen = CrossPointSettings::FOLLOW_THEME;
    s.fontSize = CrossPointSettings::EXTRA_LARGE;
    s.cleanSleepRefresh = 0;

    CHECK(settings_backup::backup(s));

    // Change settings again
    s.sleepScreen = CrossPointSettings::DARK;
    s.fontSize = CrossPointSettings::SMALL;
    s.cleanSleepRefresh = 1;

    // Restore
    CHECK(settings_backup::restore(s));

    // Verify they match the second backup
    CHECK(s.sleepScreen == CrossPointSettings::FOLLOW_THEME);
    CHECK(s.fontSize == CrossPointSettings::EXTRA_LARGE);
    CHECK(s.cleanSleepRefresh == 0);
  }
}

#include <cstring>
#include <string>

#include "doctest/doctest.h"
#include "src/CrossPointSettings.h"
#include "src/JsonSettingsIO.h"
#include "test/mock/HalStorage.h"

namespace {

constexpr const char* kSettingsPath = "/.crosspoint/settings.json";

CrossPointSettings& resetSettingsState() {
  Storage.reset();
  CrossPointSettings::resetToDefaults();
  return CrossPointSettings::getInstance();
}

}  // namespace

TEST_CASE("settings persistence saves golden shape") {
  CrossPointSettings& settings = resetSettingsState();
  settings.sleepScreen = CrossPointSettings::LIGHT;
  settings.fontSize = CrossPointSettings::LARGE;
  settings.refreshFrequency = CrossPointSettings::REFRESH_10;
  settings.sleepTimeoutMinutes = 23;
  settings.stayAwakeWhileCharging = 1;
  settings.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR;
  settings.hideBatteryPercentage = CrossPointSettings::HIDE_READER;
  settings.timeMode = CrossPointSettings::TIME_MODE_LOCAL;
  settings.timeZoneOffset = 14;
  settings.terminusSleepEnabled = 1;
  settings.timedSleepRefreshInterval = 3;
  std::strncpy(settings.userFontPath, "/fonts/Golden.ttf", sizeof(settings.userFontPath) - 1);
  settings.userFontPath[sizeof(settings.userFontPath) - 1] = '\0';

  REQUIRE(settings.saveToFile());
  const std::string json = Storage.readFile(kSettingsPath).c_str();
  REQUIRE(!json.empty());
  CHECK(json.find("\"settingsVersion\"") != std::string::npos);
  CHECK(json.find("\"sleepScreen\":1") != std::string::npos);
  CHECK(json.find("\"fontSize\":2") != std::string::npos);
  CHECK(json.find("\"refreshFrequency\":2") != std::string::npos);
  CHECK(json.find("\"sleepTimeoutMinutes\":23") != std::string::npos);
  CHECK(json.find("\"stayAwakeWhileCharging\":1") != std::string::npos);
  CHECK(json.find("\"opdsFilenameFormat\":1") != std::string::npos);
  CHECK(json.find("\"terminusSleepEnabled\":1") != std::string::npos);
  CHECK(json.find("\"timedSleepRefreshInterval\":3") != std::string::npos);
  CHECK(json.find("\"userFontPath\":\"/fonts/Golden.ttf\"") != std::string::npos);
}

TEST_CASE("settings persistence loads current shape literal") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json =
      "{\"version\":1,\"sleepScreen\":1,\"fontSize\":2,\"refreshFrequency\":2,"
      "\"sleepTimeoutMinutes\":23,\"stayAwakeWhileCharging\":1,\"opdsFilenameFormat\":1,"
      "\"hideBatteryPercentage\":1,"
      "\"timeMode\":1,\"timeZoneOffset\":14,\"terminusSleepEnabled\":1,"
      "\"timedSleepRefreshInterval\":5,\"userFontPath\":\"/fonts/Golden.ttf\"}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.sleepScreen == CrossPointSettings::LIGHT);
  CHECK(settings.fontSize == CrossPointSettings::LARGE);
  CHECK(settings.refreshFrequency == CrossPointSettings::REFRESH_10);
  CHECK(settings.sleepTimeoutMinutes == 23);
  CHECK(settings.stayAwakeWhileCharging == 1);
  CHECK(settings.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR);
  CHECK(settings.hideBatteryPercentage == CrossPointSettings::HIDE_READER);
  CHECK(settings.timeMode == CrossPointSettings::TIME_MODE_LOCAL);
  CHECK(settings.timeZoneOffset == 14);
  CHECK(settings.terminusSleepEnabled == 1);
  CHECK(settings.timedSleepRefreshInterval == 5);
  CHECK(settings.getTimedRefreshIntervalMicros() == 24ULL * 3600ULL * 1000000ULL);
  CHECK(std::string(settings.userFontPath) == "/fonts/Golden.ttf");
}

TEST_CASE("settings persistence accepts charging screensaver refresh mode") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"version\":1,\"timedSleepRefreshInterval\":6}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.timedSleepRefreshInterval == CrossPointSettings::TIMED_REFRESH_SCREENSAVER);
  CHECK(settings.getTimedRefreshIntervalMicros() == 15ULL * 60ULL * 1000000ULL);
}

TEST_CASE("settings persistence ignores unknown future keys") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json =
      "{\"version\":1,\"fontSize\":2,\"sleepTimeoutMinutes\":19,"
      "\"someFutureKey\":42,\"futureNested\":{\"enabled\":true}}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.fontSize == CrossPointSettings::LARGE);
  CHECK(settings.sleepTimeoutMinutes == 19);
}

TEST_CASE("settings persistence keeps defaults for sparse legacy input") {
  CrossPointSettings& settings = resetSettingsState();
  const uint8_t defaultRefresh = settings.refreshFrequency;
  const uint8_t defaultHideBattery = settings.hideBatteryPercentage;
  const char* json = "{\"version\":1,\"sleepScreen\":1,\"timeZoneOffset\":10,\"fontSize\":0}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.sleepScreen == CrossPointSettings::LIGHT);
  CHECK(settings.timeZoneOffset == 10);
  CHECK(settings.fontSize == CrossPointSettings::SMALL);
  CHECK(settings.refreshFrequency == defaultRefresh);
  CHECK(settings.hideBatteryPercentage == defaultHideBattery);
}

TEST_CASE("settings persistence ignores removed key while loading siblings") {
  CrossPointSettings& settings = resetSettingsState();
  const char* withRemovedKey =
      "{\"version\":1,\"usbMscPromptOnConnect\":true,\"fontSize\":3,\"sleepTimeoutMinutes\":20}";
  const char* withoutRemovedKey = "{\"version\":1,\"fontSize\":3,\"sleepTimeoutMinutes\":20}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, withRemovedKey, nullptr));
  const uint8_t withRemovedFontSize = settings.fontSize;
  const uint8_t withRemovedTimeout = settings.sleepTimeoutMinutes;
  const uint8_t withRemovedBackgroundOnCharge = settings.backgroundServerOnCharge;

  CrossPointSettings& baseline = resetSettingsState();
  REQUIRE(JsonSettingsIO::loadSettings(baseline, withoutRemovedKey, nullptr));
  CHECK(settings.fontSize == CrossPointSettings::EXTRA_LARGE);
  CHECK(settings.sleepTimeoutMinutes == 20);
  CHECK(withRemovedFontSize == baseline.fontSize);
  CHECK(withRemovedTimeout == baseline.sleepTimeoutMinutes);
  CHECK(withRemovedBackgroundOnCharge == baseline.backgroundServerOnCharge);
}

TEST_CASE("settings persistence clamps hostile enum values and ignores wrong types") {
  CrossPointSettings& settings = resetSettingsState();
  const uint8_t defaultSleepTimeout = settings.sleepTimeoutMinutes;
  const char* json =
      "{\"version\":1,\"fontSize\":255,\"sleepScreen\":99,\"sleepTimeoutMinutes\":\"fast\","
      "\"stayAwakeWhileCharging\":7,\"terminusSleepEnabled\":7,\"timedSleepRefreshInterval\":255}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  settings.validateAndClamp();
  CHECK(settings.fontSize == CrossPointSettings::MEDIUM);
  CHECK(settings.sleepScreen == CrossPointSettings::DARK);
  CHECK(settings.sleepTimeoutMinutes == defaultSleepTimeout);
  CHECK(settings.stayAwakeWhileCharging == 1);
  CHECK(settings.terminusSleepEnabled == 1);
  CHECK(settings.timedSleepRefreshInterval == 0);
}

TEST_CASE("settings migration: legacy bottom migrates to STATUS_BAR_ON and triggers resave") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"globalStatusBarPosition\":1}";
  bool needsResave = false;

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, &needsResave));
  CHECK(settings.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_ON);
  CHECK(needsResave == true);
}

TEST_CASE("settings migration: legacy top is untouched") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"globalStatusBarPosition\":0}";
  bool needsResave = false;

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, &needsResave));
  CHECK(settings.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_ON);
  CHECK(needsResave == false);
}

TEST_CASE("settings migration: legacy off is untouched") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"globalStatusBarPosition\":2}";
  bool needsResave = false;

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, &needsResave));
  CHECK(settings.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_OFF);
  CHECK(needsResave == false);
}

TEST_CASE("settings migration: versioned reader-only survives") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"settingsVersion\":1,\"globalStatusBarPosition\":1}";
  bool needsResave = false;

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, &needsResave));
  CHECK(settings.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_READER_ONLY);
  CHECK(needsResave == false);
}

TEST_CASE("settings migration: round trip preserves STATUS_BAR_READER_ONLY") {
  CrossPointSettings& settings = resetSettingsState();
  settings.globalStatusBarPosition = CrossPointSettings::STATUS_BAR_READER_ONLY;

  REQUIRE(settings.saveToFile());
  const std::string json = Storage.readFile(kSettingsPath).c_str();
  REQUIRE(!json.empty());
  CHECK(json.find("\"settingsVersion\":1") != std::string::npos);
  CHECK(json.find("\"globalStatusBarPosition\":1") != std::string::npos);

  CrossPointSettings& reloaded = resetSettingsState();
  bool needsResave = false;
  REQUIRE(JsonSettingsIO::loadSettings(reloaded, json.c_str(), &needsResave));
  CHECK(reloaded.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_READER_ONLY);
  CHECK(needsResave == false);
}

TEST_CASE("settings round trip preserves language through the shared serializer") {
  CrossPointSettings& settings = resetSettingsState();
  settings.language = 1;  // any non-zero valid index

  REQUIRE(settings.saveToFile());
  const std::string json = Storage.readFile(kSettingsPath).c_str();
  REQUIRE(json.find("\"language\"") != std::string::npos);

  CrossPointSettings& reloaded = resetSettingsState();
  bool needsResave = false;
  REQUIRE(JsonSettingsIO::loadSettings(reloaded, json.c_str(), &needsResave));
  CHECK(reloaded.language == 1);
}

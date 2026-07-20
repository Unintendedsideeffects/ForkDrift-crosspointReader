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
  settings.opdsFilenameFormat = CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR;
  settings.hideBatteryPercentage = CrossPointSettings::HIDE_READER;
  settings.timeMode = CrossPointSettings::TIME_MODE_LOCAL;
  settings.timeZoneOffset = 14;
  std::strncpy(settings.userFontPath, "/fonts/Golden.ttf", sizeof(settings.userFontPath) - 1);
  settings.userFontPath[sizeof(settings.userFontPath) - 1] = '\0';

  REQUIRE(settings.saveToFile());
  const std::string json = Storage.readFile(kSettingsPath).c_str();
  REQUIRE(!json.empty());
  CHECK(json.find("\"version\"") != std::string::npos);
  CHECK(json.find("\"sleepScreen\":1") != std::string::npos);
  CHECK(json.find("\"fontSize\":2") != std::string::npos);
  CHECK(json.find("\"refreshFrequency\":2") != std::string::npos);
  CHECK(json.find("\"sleepTimeoutMinutes\":23") != std::string::npos);
  CHECK(json.find("\"opdsFilenameFormat\":1") != std::string::npos);
  CHECK(json.find("\"userFontPath\":\"/fonts/Golden.ttf\"") != std::string::npos);
}

TEST_CASE("settings persistence loads current shape literal") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json =
      "{\"version\":1,\"sleepScreen\":1,\"fontSize\":2,\"refreshFrequency\":2,"
      "\"sleepTimeoutMinutes\":23,\"opdsFilenameFormat\":1,\"hideBatteryPercentage\":1,"
      "\"timeMode\":1,\"timeZoneOffset\":14,\"userFontPath\":\"/fonts/Golden.ttf\"}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.sleepScreen == CrossPointSettings::LIGHT);
  CHECK(settings.fontSize == CrossPointSettings::LARGE);
  CHECK(settings.refreshFrequency == CrossPointSettings::REFRESH_10);
  CHECK(settings.sleepTimeoutMinutes == 23);
  CHECK(settings.opdsFilenameFormat == CrossPointSettings::OPDS_FILENAME_TITLE_AUTHOR);
  CHECK(settings.hideBatteryPercentage == CrossPointSettings::HIDE_READER);
  CHECK(settings.timeMode == CrossPointSettings::TIME_MODE_LOCAL);
  CHECK(settings.timeZoneOffset == 14);
  CHECK(std::string(settings.userFontPath) == "/fonts/Golden.ttf");
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
  const char* json = "{\"version\":1,\"fontSize\":255,\"sleepScreen\":99,\"sleepTimeoutMinutes\":\"fast\"}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  settings.validateAndClamp();
  CHECK(settings.fontSize == CrossPointSettings::MEDIUM);
  CHECK(settings.sleepScreen == CrossPointSettings::DARK);
  CHECK(settings.sleepTimeoutMinutes == defaultSleepTimeout);
}

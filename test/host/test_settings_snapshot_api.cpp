#include "doctest/doctest.h"

#include <ArduinoJson.h>
#include <cstring>
#include <string>

#include "network/SettingsSnapshotApi.h"
#include "src/CrossPointSettings.h"

TEST_CASE("settings snapshot api serializes selected fields") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.sleepScreen = CrossPointSettings::CUSTOM;
  s.sleepScreenSource = CrossPointSettings::SLEEP_SOURCE_ALL;
  s.uiTheme = CrossPointSettings::FORK_DRIFT;
  s.timeZoneOffset = 23;
  std::strncpy(s.sleepPinnedPath, "/sleep/pinned.bmp", sizeof(s.sleepPinnedPath) - 1);
  std::strncpy(s.selectedOtaBundle, "bundle-123", sizeof(s.selectedOtaBundle) - 1);
  std::strncpy(s.installedOtaBundle, "bundle-456", sizeof(s.installedOtaBundle) - 1);
  std::strncpy(s.deviceName, "CrossPoint Test", sizeof(s.deviceName) - 1);

  const String json = network::buildSettingsSnapshotJson(s);

  JsonDocument doc;
  CHECK(!deserializeJson(doc, json.c_str()));
  CHECK((doc["sleepScreen"] | -1) == static_cast<int>(CrossPointSettings::CUSTOM));
  CHECK((doc["sleepScreenSource"] | -1) == static_cast<int>(CrossPointSettings::SLEEP_SOURCE_ALL));
  CHECK((doc["uiTheme"] | -1) == static_cast<int>(CrossPointSettings::FORK_DRIFT));
  CHECK(doc["timeZoneOffset"] == 23);
  CHECK(std::string(doc["sleepPinnedPath"] | "") == "/sleep/pinned.bmp");
  CHECK(std::string(doc["selectedOtaBundle"] | "") == "bundle-123");
  CHECK(std::string(doc["installedOtaBundle"] | "") == "bundle-456");
  CHECK(std::string(doc["deviceName"] | "") == "CrossPoint Test");
}

#include "doctest/doctest.h"

#include <ArduinoJson.h>
#include <cstring>
#include <string>

#include "network/SettingsSnapshotApi.h"
#include "src/CrossPointSettings.h"

TEST_CASE("settings snapshot api serializes selected fields") {
  CrossPointSettings& s = CrossPointSettings::getInstance();

  // Save original values so the singleton is clean for other tests.
  const auto origSleepScreen = s.sleepScreen;
  const auto origSleepScreenSource = s.sleepScreenSource;
  const auto origUiTheme = s.uiTheme;
  const auto origTimeZoneOffset = s.timeZoneOffset;
  char origSleepPinnedPath[sizeof(s.sleepPinnedPath)];
  char origSelectedOtaBundle[sizeof(s.selectedOtaBundle)];
  char origInstalledOtaBundle[sizeof(s.installedOtaBundle)];
  char origDeviceName[sizeof(s.deviceName)];
  std::memcpy(origSleepPinnedPath, s.sleepPinnedPath, sizeof(s.sleepPinnedPath));
  std::memcpy(origSelectedOtaBundle, s.selectedOtaBundle, sizeof(s.selectedOtaBundle));
  std::memcpy(origInstalledOtaBundle, s.installedOtaBundle, sizeof(s.installedOtaBundle));
  std::memcpy(origDeviceName, s.deviceName, sizeof(s.deviceName));

  s.sleepScreen = CrossPointSettings::CUSTOM;
  s.sleepScreenSource = CrossPointSettings::SLEEP_SOURCE_ALL;
  s.uiTheme = CrossPointSettings::FORK_DRIFT;
  s.timeZoneOffset = 23;
  std::strncpy(s.sleepPinnedPath, "/sleep/pinned.bmp", sizeof(s.sleepPinnedPath) - 1);
  s.sleepPinnedPath[sizeof(s.sleepPinnedPath) - 1] = '\0';
  std::strncpy(s.selectedOtaBundle, "bundle-123", sizeof(s.selectedOtaBundle) - 1);
  s.selectedOtaBundle[sizeof(s.selectedOtaBundle) - 1] = '\0';
  std::strncpy(s.installedOtaBundle, "bundle-456", sizeof(s.installedOtaBundle) - 1);
  s.installedOtaBundle[sizeof(s.installedOtaBundle) - 1] = '\0';
  std::strncpy(s.deviceName, "CrossPoint Test", sizeof(s.deviceName) - 1);
  s.deviceName[sizeof(s.deviceName) - 1] = '\0';

  const String json = network::buildSettingsSnapshotJson(s);

  // Restore singleton state before asserting so teardown is always clean.
  s.sleepScreen = origSleepScreen;
  s.sleepScreenSource = origSleepScreenSource;
  s.uiTheme = origUiTheme;
  s.timeZoneOffset = origTimeZoneOffset;
  std::memcpy(s.sleepPinnedPath, origSleepPinnedPath, sizeof(s.sleepPinnedPath));
  std::memcpy(s.selectedOtaBundle, origSelectedOtaBundle, sizeof(s.selectedOtaBundle));
  std::memcpy(s.installedOtaBundle, origInstalledOtaBundle, sizeof(s.installedOtaBundle));
  std::memcpy(s.deviceName, origDeviceName, sizeof(s.deviceName));

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
  CHECK(doc["opdsUsername"].isNull());
}

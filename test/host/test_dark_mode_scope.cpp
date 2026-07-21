#include "doctest/doctest.h"
#include "src/CrossPointSettings.h"
#include "src/JsonSettingsIO.h"
#include "test/mock/HalStorage.h"

namespace {

CrossPointSettings& resetSettingsState() {
  Storage.reset();
  CrossPointSettings::resetToDefaults();
  return CrossPointSettings::getInstance();
}

}  // namespace

TEST_CASE("dark mode scope migrates legacy darkMode true to everywhere") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"version\":1,\"darkMode\":1}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.darkModeScope == CrossPointSettings::DARK_EVERYWHERE);
  CHECK(settings.darkMode == 1);
  CHECK(settings.effectiveDarkMode(false));
  CHECK(settings.effectiveDarkMode(true));
}

TEST_CASE("dark mode scope reader only affects reader context") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"version\":1,\"darkModeScope\":1}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.darkModeScope == CrossPointSettings::DARK_READER_ONLY);
  CHECK(settings.darkMode == 0);
  CHECK_FALSE(settings.isGlobalDarkMode());
  CHECK_FALSE(settings.effectiveDarkMode(false));
  CHECK(settings.effectiveDarkMode(true));
}

TEST_CASE("dark mode scope round trip writes legacy darkMode") {
  CrossPointSettings& settings = resetSettingsState();
  settings.darkModeScope = CrossPointSettings::DARK_EVERYWHERE;
  settings.syncDarkModeLegacyField();

  REQUIRE(settings.saveToFile());
  const std::string json = Storage.readFile("/.crosspoint/settings.json").c_str();
  CHECK(json.find("\"darkModeScope\":2") != std::string::npos);
  CHECK(json.find("\"darkMode\":1") != std::string::npos);
}

TEST_CASE("dark mode scope clamps invalid values") {
  CrossPointSettings& settings = resetSettingsState();
  const char* json = "{\"version\":1,\"darkModeScope\":9}";

  REQUIRE(JsonSettingsIO::loadSettings(settings, json, nullptr));
  CHECK(settings.darkModeScope == CrossPointSettings::DARK_OFF);
}

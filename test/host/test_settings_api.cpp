#include <ArduinoJson.h>

#include <vector>

#include "CrossPointSettings.h"
#include "doctest/doctest.h"
#include "src/SettingInfo.h"
#include "src/network/SettingsApply.h"

// Exercises network::applySettingsToList — the pure value-application half of the
// POST /api/settings path. It is split out of applySettingsJson precisely so it can
// be host-tested without the SD font registry that getSettingsList() drags in
// (see plans/003 — that linkage cone is why the original test-only plan was BLOCKED).
//
// The tests build a SYNTHETIC settings list bound to real CrossPointSettings fields,
// so applying a value is observable as a change to SETTINGS.<field>.

using network::applySettingsToList;
using network::SettingsApplyTally;

namespace {

// Build a JsonDocument from a literal so each case is self-contained.
JsonDocument parse(const char* json) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  REQUIRE_FALSE(err);
  return doc;
}

}  // namespace

TEST_CASE("applySettingsToList: empty object applies nothing") {
  std::vector<SettingInfo> settings = {
      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::statusBarBattery, "tkey"),
  };
  JsonDocument doc = parse("{}");
  const SettingsApplyTally tally = applySettingsToList(doc, settings);
  CHECK(tally.appliedCount == 0);
  CHECK(tally.updatedKoreaderSettings == false);
}

TEST_CASE("applySettingsToList: unknown keys are ignored") {
  std::vector<SettingInfo> settings = {
      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::statusBarBattery, "tkey"),
  };
  JsonDocument doc = parse("{\"totallyUnknownKey\":5}");
  const SettingsApplyTally tally = applySettingsToList(doc, settings);
  CHECK(tally.appliedCount == 0);
}

TEST_CASE("applySettingsToList: TOGGLE writes 0/1 and coerces truthy ints") {
  SETTINGS.statusBarBattery = 0;
  std::vector<SettingInfo> settings = {
      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::statusBarBattery, "tkey"),
  };

  SUBCASE("set on") {
    JsonDocument doc = parse("{\"tkey\":1}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 1);
    CHECK(SETTINGS.statusBarBattery == 1);
  }
  SUBCASE("set off") {
    SETTINGS.statusBarBattery = 1;
    JsonDocument doc = parse("{\"tkey\":0}");
    applySettingsToList(doc, settings);
    CHECK(SETTINGS.statusBarBattery == 0);
  }
  SUBCASE("any truthy int becomes 1") {
    JsonDocument doc = parse("{\"tkey\":7}");
    applySettingsToList(doc, settings);
    CHECK(SETTINGS.statusBarBattery == 1);
  }
}

TEST_CASE("applySettingsToList: VALUE enforces the inclusive range") {
  // extraParagraphSpacing accepts a small numeric range; bind a [2,8] range to it.
  SETTINGS.extraParagraphSpacing = 4;
  std::vector<SettingInfo> settings = {
      SettingInfo::Value(StrId::STR_NONE_OPT, &CrossPointSettings::extraParagraphSpacing,
                         SettingInfo::ValueRange{2, 8, 1}, "vkey"),
  };

  SUBCASE("in range applies") {
    JsonDocument doc = parse("{\"vkey\":5}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 1);
    CHECK(SETTINGS.extraParagraphSpacing == 5);
  }
  SUBCASE("above max is rejected, value unchanged") {
    JsonDocument doc = parse("{\"vkey\":99}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 0);
    CHECK(SETTINGS.extraParagraphSpacing == 4);
  }
  SUBCASE("below min is rejected, value unchanged") {
    JsonDocument doc = parse("{\"vkey\":1}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 0);
    CHECK(SETTINGS.extraParagraphSpacing == 4);
  }
}

TEST_CASE("applySettingsToList: ENUM accepts in-range option indices, rejects out-of-range") {
  // 2 options -> valid indices are 0 and 1 (enumPersistedValues empty path).
  SETTINGS.sleepScreenSplit = 0;
  std::vector<SettingInfo> settings = {
      SettingInfo::Enum(StrId::STR_NONE_OPT, &CrossPointSettings::sleepScreenSplit,
                        {StrId::STR_NONE_OPT, StrId::STR_NONE_OPT}, "ekey"),
  };

  SUBCASE("valid index applies") {
    JsonDocument doc = parse("{\"ekey\":1}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 1);
    CHECK(SETTINGS.sleepScreenSplit == 1);
  }
  SUBCASE("index >= option count is rejected") {
    JsonDocument doc = parse("{\"ekey\":5}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 0);
    CHECK(SETTINGS.sleepScreenSplit == 0);
  }
  SUBCASE("negative index is rejected") {
    JsonDocument doc = parse("{\"ekey\":-1}");
    const SettingsApplyTally tally = applySettingsToList(doc, settings);
    CHECK(tally.appliedCount == 0);
  }
}

TEST_CASE("applySettingsToList: KOReader-category change flips the koreader tally") {
  std::vector<SettingInfo> settings = {
      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::statusBarBattery, "kkey", StrId::STR_KOREADER_SYNC),
  };
  JsonDocument doc = parse("{\"kkey\":1}");
  const SettingsApplyTally tally = applySettingsToList(doc, settings);
  CHECK(tally.appliedCount == 1);
  CHECK(tally.updatedKoreaderSettings == true);
}

TEST_CASE("applySettingsToList: applies several settings in one document") {
  SETTINGS.statusBarBattery = 0;
  SETTINGS.extraParagraphSpacing = 4;
  std::vector<SettingInfo> settings = {
      SettingInfo::Toggle(StrId::STR_NONE_OPT, &CrossPointSettings::statusBarBattery, "tkey"),
      SettingInfo::Value(StrId::STR_NONE_OPT, &CrossPointSettings::extraParagraphSpacing,
                         SettingInfo::ValueRange{2, 8, 1}, "vkey"),
  };
  JsonDocument doc = parse("{\"tkey\":1,\"vkey\":6,\"ignored\":3}");
  const SettingsApplyTally tally = applySettingsToList(doc, settings);
  CHECK(tally.appliedCount == 2);
  CHECK(SETTINGS.statusBarBattery == 1);
  CHECK(SETTINGS.extraParagraphSpacing == 6);
}

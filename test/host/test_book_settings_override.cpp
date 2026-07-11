#include <cstdint>
#include <string>

#include "doctest/doctest.h"
#include "src/CrossPointSettings.h"
#include "src/JsonSettingsIO.h"
#include "src/util/BookSettingsOverride.h"
#include "test/mock/HalStorage.h"

TEST_CASE("BookSettingsOverride JSON round-trip") {
  Storage.reset();

  BookSettingsOverride o;
  o.enabled = true;
  o.hasFontSize = true;
  o.fontSize = CrossPointSettings::LARGE;
  o.hasLineSpacing = true;
  o.lineSpacing = CrossPointSettings::WIDE;
  o.hasScreenMargin = true;
  o.screenMargin = 12;
  o.hasFontFamily = false;

  CHECK(o.save("/cache_roundtrip"));
  CHECK(Storage.exists("/cache_roundtrip/book_settings.json"));

  BookSettingsOverride o2;
  CHECK(BookSettingsOverride::load("/cache_roundtrip", o2));

  CHECK(o2.enabled == true);
  CHECK(o2.hasFontSize == true);
  CHECK(o2.fontSize == CrossPointSettings::LARGE);
  CHECK(o2.hasLineSpacing == true);
  CHECK(o2.lineSpacing == CrossPointSettings::WIDE);
  CHECK(o2.hasScreenMargin == true);
  CHECK(o2.screenMargin == 12);
  CHECK(o2.hasFontFamily == false);
  CHECK(o2.fontFamily == 0);
}

TEST_CASE("BookSettingsOverride applyToGlobals is masked") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.fontSize = CrossPointSettings::MEDIUM;
  s.lineSpacing = CrossPointSettings::NORMAL;
  s.screenMargin = 5;

  BookSettingsOverride o;
  o.hasFontSize = true;
  o.fontSize = CrossPointSettings::LARGE;
  o.hasLineSpacing = false;
  o.lineSpacing = CrossPointSettings::WIDE;
  o.hasScreenMargin = false;
  o.screenMargin = 10;

  o.applyToGlobals();

  CHECK(s.fontSize == CrossPointSettings::LARGE);
  CHECK(s.lineSpacing == CrossPointSettings::NORMAL);
  CHECK(s.screenMargin == 5);
}

TEST_CASE("BookSettingsOverride leak regression (35fa242c)") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.fontSize = CrossPointSettings::MEDIUM;
  CHECK(s.saveToFile());

  BookSettingsOverride o;
  o.enabled = true;
  o.hasFontSize = true;
  o.fontSize = CrossPointSettings::LARGE;

  BookSettingsOverride snapshot;
  snapshot.captureFromGlobals();  // fontSize = MEDIUM

  o.applyToGlobals();  // s.fontSize becomes LARGE
  CHECK(s.fontSize == CrossPointSettings::LARGE);

  BookSettingsScope::setActive(&o, "/cache_leak", snapshot);
  CHECK(BookSettingsScope::isEnabled() == true);

  // When we save globals preserving overrides, the persisted file must have the snapshot value (MEDIUM)
  CHECK(BookSettingsScope::saveGlobalsPreservingOverrides() == true);

  // In RAM, the per-book value must remain applied
  CHECK(s.fontSize == CrossPointSettings::LARGE);

  // Check the persisted settings on disk (should be the snapshot value: MEDIUM)
  // We change RAM first to a dummy value, loadFromFile, and assert it returns MEDIUM.
  s.fontSize = CrossPointSettings::EXTRA_LARGE;
  CHECK(s.loadFromFile() == true);
  CHECK(s.fontSize == CrossPointSettings::MEDIUM);

  BookSettingsScope::clearActive();
  CHECK(BookSettingsScope::isEnabled() == false);
}

TEST_CASE("BookSettingsOverride recordChange") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  s.fontSize = CrossPointSettings::LARGE;

  BookSettingsOverride o;
  o.enabled = true;
  o.hasFontSize = false;

  BookSettingsScope::setActive(&o, "/cache_record", BookSettingsOverride{});
  CHECK(BookSettingsScope::isEnabled() == true);

  CHECK(BookSettingsScope::recordChange("fontSize") == true);
  CHECK(o.hasFontSize == true);
  CHECK(o.fontSize == CrossPointSettings::LARGE);
  CHECK(Storage.exists("/cache_record/book_settings.json"));

  CHECK(BookSettingsScope::recordChange("invalidKey") == false);

  BookSettingsScope::clearActive();
}

TEST_CASE("BookSettingsOverride corrupt JSON handling") {
  Storage.reset();

  Storage.writeFile("/cache_corrupt/book_settings.json", "{not json");

  BookSettingsOverride o;
  o.enabled = true;
  o.hasFontSize = true;
  o.fontSize = 5;

  CHECK(BookSettingsOverride::load("/cache_corrupt", o) == false);
  CHECK(o.enabled == false);
  CHECK(o.hasFontSize == false);
  CHECK(o.fontSize == 0);
}

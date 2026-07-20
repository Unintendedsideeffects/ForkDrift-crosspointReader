#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "lib/Serialization/Serialization.h"
#include "src/CrossPointSettings.h"
#include "src/util/BookSettingsOverride.h"
#include "test/mock/HalStorage.h"

namespace {

bool floatsBitEqual(const float a, const float b) {
  static_assert(sizeof(float) == 4);
  return std::memcmp(&a, &b, sizeof(float)) == 0;
}

float roundTripPod(const float value) {
  std::ostringstream os(std::ios::binary);
  serialization::writePod(os, value);
  std::istringstream is(os.str(), std::ios::binary);
  float out = 0.0f;
  REQUIRE(serialization::readPod(is, out));
  return out;
}

void assertGetterAndPodStable(CrossPointSettings& s) {
  const float first = s.getReaderLineCompression();
  const float second = s.getReaderLineCompression();
  CHECK(floatsBitEqual(first, second));
  CHECK(floatsBitEqual(first, roundTripPod(first)));
}

}  // namespace

TEST_CASE("lineCompression getter is bit-stable and pod round-trips") {
  Storage.reset();
  CrossPointSettings& s = CrossPointSettings::getInstance();

  const std::vector<uint8_t> lineSpacings = {
      CrossPointSettings::TIGHT,
      CrossPointSettings::NORMAL,
      CrossPointSettings::WIDE,
  };

  const std::vector<uint8_t> fontFamilies = {
      CrossPointSettings::BOOKERLY,
#if ENABLE_NOTOSANS_FONTS
      CrossPointSettings::NOTOSANS,
#endif
      CrossPointSettings::OPENDYSLEXIC, CrossPointSettings::LEXENDDECA, CrossPointSettings::BITTER,
      CrossPointSettings::CHAREINK,     CrossPointSettings::USER_SD,
  };

  for (const uint8_t fontFamily : fontFamilies) {
    for (const uint8_t lineSpacing : lineSpacings) {
      INFO("fontFamily=" << static_cast<int>(fontFamily) << " lineSpacing=" << static_cast<int>(lineSpacing));

      s.fontFamily = fontFamily;
      s.sdFontFamilyName[0] = '\0';
      s.lineSpacing = lineSpacing;

      assertGetterAndPodStable(s);
    }
  }

  SUBCASE("sdFontFamilyName path") {
    s.sdFontFamilyName[0] = 'X';
    s.sdFontFamilyName[1] = '\0';
    for (const uint8_t lineSpacing : lineSpacings) {
      s.lineSpacing = lineSpacing;
      assertGetterAndPodStable(s);
    }
    s.sdFontFamilyName[0] = '\0';
  }

#if ENABLE_PER_BOOK_SETTINGS
  SUBCASE("BookSettingsOverride snapshot path") {
    s.fontFamily = CrossPointSettings::BOOKERLY;
    s.lineSpacing = CrossPointSettings::NORMAL;
    const float baseline = s.getReaderLineCompression();

    BookSettingsOverride override;
    override.hasLineSpacing = true;
    override.lineSpacing = CrossPointSettings::WIDE;
    override.applyToGlobals();
    const float overridden = s.getReaderLineCompression();
    CHECK(!floatsBitEqual(baseline, overridden));

    BookSettingsOverride snapshot;
    snapshot.captureFromGlobals();
    snapshot.lineSpacing = CrossPointSettings::NORMAL;
    snapshot.hasLineSpacing = true;
    snapshot.applyToGlobals();
    const float restored = s.getReaderLineCompression();
    CHECK(floatsBitEqual(baseline, restored));
    assertGetterAndPodStable(s);
  }
#endif
}

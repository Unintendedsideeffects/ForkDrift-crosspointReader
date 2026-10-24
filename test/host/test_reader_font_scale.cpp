#include "Epub/ReaderFontScale.h"
#include "doctest/doctest.h"

TEST_CASE("fontIdForScale maps a heading scale onto a larger registered size of the same family") {
  CHECK(fontIdForScale(UI_10_FONT_ID, 2.0f) == UI_12_FONT_ID);
  CHECK(fontIdForScale(UI_12_FONT_ID, 2.0f) == UI_12_FONT_ID);
  CHECK(fontIdForScale(UI_10_FONT_ID, 1.0f) == UI_10_FONT_ID);
#if ENABLE_BOOKERLY_FONTS
  CHECK(fontIdForScale(NOTOSERIF_14_FONT_ID, 2.0f) == NOTOSERIF_18_FONT_ID);
  CHECK(fontIdForScale(NOTOSERIF_14_FONT_ID, 1.5f) == NOTOSERIF_18_FONT_ID);
  CHECK(fontIdForScale(NOTOSERIF_14_FONT_ID, 1.2f) == NOTOSERIF_16_FONT_ID);
  CHECK(fontIdForScale(NOTOSERIF_14_FONT_ID, 1.0f) == NOTOSERIF_14_FONT_ID);
  CHECK(fontIdForScale(NOTOSERIF_18_FONT_ID, 2.0f) == NOTOSERIF_18_FONT_ID);
#elif ENABLE_NOTOSANS_FONTS
  CHECK(fontIdForScale(NOTOSANS_14_FONT_ID, 2.0f) == NOTOSANS_18_FONT_ID);
  CHECK(fontIdForScale(NOTOSANS_18_FONT_ID, 2.0f) == NOTOSANS_18_FONT_ID);
#elif ENABLE_LEXENDDECA_FONTS
  CHECK(fontIdForScale(LEXENDDECA_14_FONT_ID, 2.0f) == LEXENDDECA_18_FONT_ID);
  CHECK(fontIdForScale(LEXENDDECA_18_FONT_ID, 2.0f) == LEXENDDECA_18_FONT_ID);
#elif ENABLE_BITTER_FONTS
  CHECK(fontIdForScale(BITTER_14_FONT_ID, 2.0f) == BITTER_18_FONT_ID);
  CHECK(fontIdForScale(BITTER_18_FONT_ID, 2.0f) == BITTER_18_FONT_ID);
#elif ENABLE_CHAREINK_FONTS
  CHECK(fontIdForScale(CHAREINK_14_FONT_ID, 2.0f) == CHAREINK_18_FONT_ID);
  CHECK(fontIdForScale(CHAREINK_18_FONT_ID, 2.0f) == CHAREINK_18_FONT_ID);
#endif
}

TEST_CASE("fontIdForScale leaves an unknown or SD-like id unchanged") {
  CHECK(fontIdForScale(123456789, 2.0f) == 123456789);
  CHECK(fontIdForScale(0, 1.5f) == 0);
}

TEST_CASE("fontIdForScale snaps a shrink onto a smaller registered size of the same family") {
  CHECK(fontIdForScale(UI_10_FONT_ID, 0.67f) == UI_8_FONT_ID);
  CHECK(fontIdForScale(UI_8_FONT_ID, 0.5f) == UI_8_FONT_ID);
#if ENABLE_BOOKERLY_FONTS
  CHECK(fontIdForScale(NOTOSERIF_14_FONT_ID, 0.67f) == NOTOSERIF_12_FONT_ID);
  CHECK(fontIdForScale(NOTOSERIF_12_FONT_ID, 0.5f) == NOTOSERIF_12_FONT_ID);
#elif ENABLE_NOTOSANS_FONTS
  CHECK(fontIdForScale(NOTOSANS_14_FONT_ID, 0.67f) == NOTOSANS_12_FONT_ID);
#endif
}

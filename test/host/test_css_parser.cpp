#include "CssParser.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

TEST_CASE("CssParser::releaseMemory drops parsed rules") {
  Storage.reset();
  HalFile out;
  REQUIRE(Storage.openFileForWrite("TEST", "/sheet.css", out));
  constexpr const char kCss[] = "p { text-align: justify; font-weight: bold; }\n";
  REQUIRE(out.write(reinterpret_cast<const uint8_t*>(kCss), sizeof(kCss) - 1) == sizeof(kCss) - 1);
  out.close();

  HalFile in;
  REQUIRE(Storage.openFileForRead("TEST", "/sheet.css", in));
  CssParser parser;
  REQUIRE(parser.loadFromStream(in));
  REQUIRE(parser.ruleCount() >= 1);
  REQUIRE_FALSE(parser.empty());

  parser.releaseMemory();
  CHECK(parser.empty());
  CHECK(parser.ruleCount() == 0);
}

// Absorbed from upstream 52444a0c9: "!important" used to be stripped only for
// display/direction, so any other property carrying it failed to parse and was dropped.
TEST_CASE("CssParser applies declarations marked !important for every property") {
  const CssStyle style = CssParser::parseInlineStyle(
      "text-align: center !important; font-weight: bold!important; text-indent: 2em !important");
  CHECK(style.defined.textAlign);
  CHECK(style.textAlign == CssTextAlign::Center);
  CHECK(style.defined.fontWeight);
  CHECK(style.fontWeight == CssFontWeight::Bold);
  CHECK(style.defined.textIndent);

  const CssStyle hidden = CssParser::parseInlineStyle("display: none !important");
  CHECK(hidden.hasDisplay());
  CHECK(hidden.display == CssDisplay::None);
}

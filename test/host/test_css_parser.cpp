#include <vector>

#include "CssParser.h"
#include "doctest/doctest.h"
#include "test/mock/Arduino.h"
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
  REQUIRE(parser.loadFromStream(in) == CssParser::ParseResult::Complete);
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

TEST_CASE("CssParser::parseInlineStyle vertical-align super sub and absent") {
  const CssStyle superStyle = CssParser::parseInlineStyle("vertical-align: super");
  CHECK(superStyle.hasVerticalAlign());
  CHECK(superStyle.verticalAlign == CssVerticalAlign::Super);

  const CssStyle subStyle = CssParser::parseInlineStyle("vertical-align: sub");
  CHECK(subStyle.hasVerticalAlign());
  CHECK(subStyle.verticalAlign == CssVerticalAlign::Sub);

  const CssStyle absent = CssParser::parseInlineStyle("font-weight: bold");
  CHECK_FALSE(absent.hasVerticalAlign());
}

TEST_CASE("CssParser::shouldPersistCache only Complete") {
  CHECK(CssParser::shouldPersistCache(CssParser::ParseResult::Complete));
  CHECK_FALSE(CssParser::shouldPersistCache(CssParser::ParseResult::Partial));
  CHECK_FALSE(CssParser::shouldPersistCache(CssParser::ParseResult::Error));
}

TEST_CASE("CssParser::loadFromStream returns Partial when rule growth is refused") {
  Storage.reset();
  HalFile out;
  REQUIRE(Storage.openFileForWrite("TEST", "/low-heap.css", out));
  constexpr const char kCss[] = "p { text-align: justify; }\n";
  REQUIRE(out.write(reinterpret_cast<const uint8_t*>(kCss), sizeof(kCss) - 1) == sizeof(kCss) - 1);
  out.close();

  HalFile in;
  REQUIRE(Storage.openFileForRead("TEST", "/low-heap.css", in));
  CssParser parser;
  const size_t savedFree = ESP.overrideFreeHeap;
  const size_t savedMax = ESP.overrideMaxAllocHeap;
  ESP.overrideFreeHeap = 1000;
  ESP.overrideMaxAllocHeap = 1000;
  const auto result = parser.loadFromStream(in);
  ESP.overrideFreeHeap = savedFree;
  ESP.overrideMaxAllocHeap = savedMax;
  CHECK(result == CssParser::ParseResult::Partial);
  CHECK(parser.empty());
}

TEST_CASE("CssParser Partial parse does not persist a cache file") {
  Storage.reset();
  CssParser parser("/css-partial");
  CHECK_FALSE(parser.hasCache());
  CHECK_FALSE(CssParser::shouldPersistCache(CssParser::ParseResult::Partial));
  CHECK_FALSE(parser.loadFromCache());
}

TEST_CASE("CssParser descendant rules round-trip a Complete cache") {
  Storage.reset();
  HalFile out;
  REQUIRE(Storage.openFileForWrite("TEST", "/desc.css", out));
  constexpr const char kCss[] = "p { text-align: left; }\ndiv p { text-indent: 1em; }\n";
  REQUIRE(out.write(reinterpret_cast<const uint8_t*>(kCss), sizeof(kCss) - 1) == sizeof(kCss) - 1);
  out.close();

  HalFile in;
  REQUIRE(Storage.openFileForRead("TEST", "/desc.css", in));
  CssParser parser;
  REQUIRE(parser.loadFromStream(in) == CssParser::ParseResult::Complete);

  HalFile cacheOut;
  REQUIRE(Storage.openFileForWrite("TEST", "/css_rules.cache", cacheOut));
  REQUIRE(parser.saveToCache(cacheOut));
  cacheOut.close();

  CssParser loaded;
  HalFile cacheIn;
  REQUIRE(Storage.openFileForRead("TEST", "/css_rules.cache", cacheIn));
  REQUIRE(loaded.loadFromCache(cacheIn));
  cacheIn.close();

  std::vector<CssAncestorEntry> ancestors;
  ancestors.push_back({0, "div", ""});
  const CssStyle style = loaded.resolveStyle("p", "", ancestors);
  CHECK(style.hasTextIndent());
  CHECK(style.hasTextAlign());
  CHECK(style.textAlign == CssTextAlign::Left);
}

TEST_CASE("CssParser parses font-size lengths, keywords, percent, and !important") {
  const CssStyle em = CssParser::parseInlineStyle("font-size: 1.5em");
  CHECK(em.hasFontSize());
  CHECK(em.fontSize.unit == CssUnit::Em);
  CHECK(em.fontSize.value == doctest::Approx(1.5f));

  const CssStyle rem = CssParser::parseInlineStyle("font-size: 2rem !important");
  CHECK(rem.hasFontSize());
  CHECK(rem.fontSize.unit == CssUnit::Rem);
  CHECK(rem.fontSize.value == doctest::Approx(2.0f));

  const CssStyle px = CssParser::parseInlineStyle("font-size: 16px");
  CHECK(px.hasFontSize());
  CHECK(px.fontSize.unit == CssUnit::Rem);
  CHECK(px.fontSize.value == doctest::Approx(1.0f));

  const CssStyle percent = CssParser::parseInlineStyle("font-size: 150%");
  CHECK(percent.hasFontSize());
  CHECK(percent.fontSize.unit == CssUnit::Em);
  CHECK(percent.fontSize.value == doctest::Approx(1.5f));

  const CssStyle large = CssParser::parseInlineStyle("font-size: xx-large");
  CHECK(large.hasFontSize());
  CHECK(large.fontSize.unit == CssUnit::Rem);
  CHECK(large.fontSize.value == doctest::Approx(2.0f));
}

TEST_CASE("CssParser parses page-break and break-* as forced page breaks") {
  const CssStyle beforeAlways = CssParser::parseInlineStyle("page-break-before: always");
  CHECK(beforeAlways.hasPageBreakBefore());
  CHECK(beforeAlways.pageBreakBefore);

  const CssStyle beforePage = CssParser::parseInlineStyle("break-before: page !important");
  CHECK(beforePage.hasPageBreakBefore());
  CHECK(beforePage.pageBreakBefore);

  const CssStyle afterAlways = CssParser::parseInlineStyle("page-break-after: always");
  CHECK(afterAlways.hasPageBreakAfter());
  CHECK(afterAlways.pageBreakAfter);

  const CssStyle afterPage = CssParser::parseInlineStyle("break-after: page");
  CHECK(afterPage.hasPageBreakAfter());
  CHECK(afterPage.pageBreakAfter);

  const CssStyle avoid = CssParser::parseInlineStyle("page-break-before: avoid");
  CHECK(avoid.hasPageBreakBefore());
  CHECK_FALSE(avoid.pageBreakBefore);
}

TEST_CASE("CssParser keeps margin shorthand when mixed with font-size") {
  const CssStyle style = CssParser::parseInlineStyle("margin: 1em 2em; font-size: 1.2em");
  CHECK(style.hasMarginTop());
  CHECK(style.hasMarginRight());
  CHECK(style.hasMarginBottom());
  CHECK(style.hasMarginLeft());
  CHECK(style.marginTop.value == doctest::Approx(1.0f));
  CHECK(style.marginRight.value == doctest::Approx(2.0f));
  CHECK(style.marginLeft.value == doctest::Approx(2.0f));
  CHECK(style.hasFontSize());
  CHECK(style.fontSize.value == doctest::Approx(1.2f));
}

TEST_CASE("CssParser cache round-trips font-size and page-break") {
  Storage.reset();
  HalFile out;
  REQUIRE(Storage.openFileForWrite("TEST", "/sheet.css", out));
  constexpr const char kCss[] = "h1 { font-size: 2em; page-break-before: always; }\n";
  REQUIRE(out.write(reinterpret_cast<const uint8_t*>(kCss), sizeof(kCss) - 1) == sizeof(kCss) - 1);
  out.close();

  HalFile in;
  REQUIRE(Storage.openFileForRead("TEST", "/sheet.css", in));
  CssParser parser;
  REQUIRE(parser.loadFromStream(in) == CssParser::ParseResult::Complete);
  in.close();

  HalFile cacheOut;
  REQUIRE(Storage.openFileForWrite("TEST", "/css.cache", cacheOut));
  REQUIRE(parser.saveToCache(cacheOut));
  cacheOut.close();

  HalFile cacheIn;
  REQUIRE(Storage.openFileForRead("TEST", "/css.cache", cacheIn));
  CssParser loaded;
  REQUIRE(loaded.loadFromCache(cacheIn));
  cacheIn.close();

  const CssStyle style = loaded.resolveStyle("h1", "");
  CHECK(style.hasFontSize());
  CHECK(style.fontSize.unit == CssUnit::Em);
  CHECK(style.fontSize.value == doctest::Approx(2.0f));
  CHECK(style.hasPageBreakBefore());
  CHECK(style.pageBreakBefore);
}

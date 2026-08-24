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

#include <cstring>
#include <string>
#include <vector>

#include "Utf8.h"
#include "doctest/doctest.h"

namespace {

const std::string kCombAcute = "\xCC\x81";
const std::string kJamoG = "\xE1\x84\x80";
const std::string kJamoH = "\xE1\x84\x92";
const std::string kJamoA = "\xE1\x85\xA1";
const std::string kJamoN = "\xE1\x86\xAB";
const std::string kJamoNg = "\xE1\x86\xBC";

std::string composeInPlace(const std::string& input) {
  std::string buffer = input;
  utf8ComposeNfcInPlace(buffer.data());
  buffer.resize(std::strlen(buffer.c_str()));
  return buffer;
}

}  // namespace

TEST_CASE("utf8 compose Hangul LV LVT and NFD filenames on both APIs") {
  const std::vector<bool> inPlace = {false, true};
  for (const bool useInPlace : inPlace) {
    const auto compose = [useInPlace](const std::string& in) {
      return useInPlace ? composeInPlace(in) : utf8ComposeNfc(in);
    };
    CHECK(compose(kJamoH + kJamoA) == "\xED\x95\x98");
    CHECK(compose(kJamoH + kJamoA + kJamoN) == "\xED\x95\x9C");
    CHECK(compose(kJamoH + kJamoA + kJamoN + kJamoG + kJamoA + kJamoNg) == "\xED\x95\x9C\xEA\xB0\x95");
    const std::string nfdFilename =
        "\xE1\x84\x92\xE1\x85\xA2\xE1\x84\x8F\xE1\x85\xA5\xE1\x84\x8B\xE1\x85\xAA"
        " "
        "\xE1\x84\x92\xE1\x85\xAA\xE1\x84\x80\xE1\x85\xA1"
        ".epub";
    CHECK(compose(nfdFilename) == "\xED\x95\xB4\xEC\xBB\xA4\xEC\x99\x80 \xED\x99\x94\xEA\xB0\x80.epub");
    CHECK(compose(kJamoH) == kJamoH);
    CHECK(compose("\xED\x95\x9C\xEA\xB0\x95") == "\xED\x95\x9C\xEA\xB0\x95");
    const std::string georgian = "\xE1\x83\xA5\xE1\x83\x90";
    CHECK(compose(georgian) == georgian);
    CHECK(compose("e" + kCombAcute) == "\xC3\xA9");
  }
}

TEST_CASE("utf8ComposeNfcInPlace never grows the buffer") {
  char buffer[] = {'a', '\xCC', '\x82', '\xCC', '\x81', '\0', '!'};
  utf8ComposeNfcInPlace(buffer);
  CHECK(std::strcmp(buffer, "\xE1\xBA\xA5") == 0);
  CHECK(buffer[5] == '\0');
  CHECK(buffer[6] == '!');
}

TEST_CASE("utf8ComposeNfcInPlace preserves malformed and truncated bytes") {
  char buffer[] =
      "\xFF"
      "e\xCC\x81"
      "\xE1\x84";
  utf8ComposeNfcInPlace(buffer);
  CHECK(std::strcmp(buffer, "\xFF\xC3\xA9\xE1\x84") == 0);
}

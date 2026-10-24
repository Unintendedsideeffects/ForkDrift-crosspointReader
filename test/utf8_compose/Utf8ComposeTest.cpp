#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Utf8.h"

namespace {

const std::string kCombGrave = "\xCC\x80";
const std::string kCombAcute = "\xCC\x81";
const std::string kCombCirc = "\xCC\x82";
const std::string kCombDotBelow = "\xCC\xA3";

}  // namespace

class Utf8ComposeNfc : public testing::TestWithParam<bool> {
 protected:
  std::string compose(const std::string& input) {
    if (!GetParam()) return utf8ComposeNfc(input);
    std::string buffer = input;
    utf8ComposeNfcInPlace(buffer.data());
    buffer.resize(strlen(buffer.c_str()));
    return buffer;
  }
};

INSTANTIATE_TEST_SUITE_P(StringAndInPlace, Utf8ComposeNfc, testing::Bool());

TEST_P(Utf8ComposeNfc, PassesThroughAsciiAndNfc) {
  EXPECT_EQ(compose(""), "");
  EXPECT_EQ(compose("hello world"), "hello world");
  EXPECT_EQ(compose("caf\xC3\xA9"), "caf\xC3\xA9");
}

TEST_P(Utf8ComposeNfc, ComposesSingleMark) {
  EXPECT_EQ(compose("e" + kCombAcute), "\xC3\xA9");
  EXPECT_EQ(compose("a" + kCombGrave), "\xC3\xA0");
}

TEST_P(Utf8ComposeNfc, ComposesStackedVietnameseMarks) {
  EXPECT_EQ(compose("a" + kCombCirc + kCombAcute), "\xE1\xBA\xA5");
  EXPECT_EQ(compose("a" + kCombDotBelow + kCombCirc), "\xE1\xBA\xAD");
}

TEST_P(Utf8ComposeNfc, LeavesUncomposableMarksIntact) {
  const std::string in = "q" + kCombAcute;
  EXPECT_EQ(compose(in), in);
}

TEST_P(Utf8ComposeNfc, HandlesLeadingMark) { EXPECT_EQ(compose(kCombAcute), kCombAcute); }

TEST_P(Utf8ComposeNfc, ComposesWithinWord) {
  EXPECT_EQ(compose("Ti" + std::string("e") + kCombCirc + kCombAcute + "ng"), "Ti\xE1\xBA\xBFng");
}

namespace {
const std::string kJamoG = "\xE1\x84\x80";
const std::string kJamoH = "\xE1\x84\x92";
const std::string kJamoA = "\xE1\x85\xA1";
const std::string kJamoN = "\xE1\x86\xAB";
const std::string kJamoNg = "\xE1\x86\xBC";
}  // namespace

TEST_P(Utf8ComposeNfc, ComposesHangulLv) { EXPECT_EQ(compose(kJamoH + kJamoA), "\xED\x95\x98"); }

TEST_P(Utf8ComposeNfc, ComposesHangulLvt) { EXPECT_EQ(compose(kJamoH + kJamoA + kJamoN), "\xED\x95\x9C"); }

TEST_P(Utf8ComposeNfc, ComposesHangulWord) {
  const std::string nfd = kJamoH + kJamoA + kJamoN + kJamoG + kJamoA + kJamoNg;
  EXPECT_EQ(compose(nfd), "\xED\x95\x9C\xEA\xB0\x95");
}

TEST_P(Utf8ComposeNfc, ComposesRealNfdFilename) {
  const std::string nfd =
      "\xE1\x84\x92\xE1\x85\xA2\xE1\x84\x8F\xE1\x85\xA5\xE1\x84\x8B\xE1\x85\xAA"
      " "
      "\xE1\x84\x92\xE1\x85\xAA\xE1\x84\x80\xE1\x85\xA1"
      ".epub";
  EXPECT_EQ(compose(nfd), "\xED\x95\xB4\xEC\xBB\xA4\xEC\x99\x80 \xED\x99\x94\xEA\xB0\x80.epub");
}

TEST_P(Utf8ComposeNfc, LeavesUncomposableJamoIntact) {
  EXPECT_EQ(compose(kJamoH), kJamoH);
  EXPECT_EQ(compose(kJamoA + kJamoN), kJamoA + kJamoN);
  EXPECT_EQ(compose(kJamoH + "a"), kJamoH + "a");
}

TEST_P(Utf8ComposeNfc, PassesThroughPrecomposedHangul) {
  EXPECT_EQ(compose("\xED\x95\x9C\xEA\xB0\x95"), "\xED\x95\x9C\xEA\xB0\x95");
}

TEST_P(Utf8ComposeNfc, LeavesOtherE1ScriptsIntact) {
  const std::string georgian = "\xE1\x83\xA5\xE1\x83\x90";
  EXPECT_EQ(compose(georgian), georgian);
  const std::string cherokee = "\xE1\x8F\xA3";
  EXPECT_EQ(compose(cherokee), cherokee);
}

TEST_P(Utf8ComposeNfc, ComposesFolderDisplayAndPreservesDelimiters) {
  EXPECT_EQ(compose("[" + kJamoH + kJamoA + kJamoN + "]"), "[\xED\x95\x9C]");
  EXPECT_EQ(compose("[cafe" + kCombAcute + "]"), "[caf\xC3\xA9]");
}

TEST_P(Utf8ComposeNfc, DoesNotComposeAcrossUncomposableMark) {
  EXPECT_EQ(compose(kJamoH + kCombAcute + kJamoA), kJamoH + kCombAcute + kJamoA);
}

TEST_P(Utf8ComposeNfc, PreservesFourByteCodepointsAfterComposition) {
  EXPECT_EQ(compose("e" + kCombAcute + "\xF0\x9F\x93\x96.epub"), "\xC3\xA9\xF0\x9F\x93\x96.epub");
}

TEST(Utf8ComposeNfcInPlace, FitsWithinOriginalBuffer) {
  char buffer[] = {'a', '\xCC', '\x82', '\xCC', '\x81', '\0', '!'};
  utf8ComposeNfcInPlace(buffer);
  EXPECT_STREQ(buffer, "\xE1\xBA\xA5");
  EXPECT_EQ(buffer[5], '\0');
  EXPECT_EQ(buffer[6], '!');
}

TEST(Utf8ComposeNfcInPlace, PreservesMalformedAndTruncatedBytes) {
  char buffer[] =
      "\xFF"
      "e\xCC\x81"
      "\xE1\x84";
  utf8ComposeNfcInPlace(buffer);
  EXPECT_STREQ(buffer, "\xFF\xC3\xA9\xE1\x84");
}

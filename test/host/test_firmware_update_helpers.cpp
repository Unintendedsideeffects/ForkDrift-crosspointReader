#include "doctest/doctest.h"
#include "src/util/FirmwareUpdateHelpers.h"

using firmware_update::advanceMarkerMatch;
using firmware_update::extractVersionAfterMarker;
using firmware_update::fnv1aUpdate;
using firmware_update::isFirmwareVersionChar;

TEST_CASE("fnv1aUpdate: computes standard 32-bit FNV-1a") {
  const uint32_t fnvBasis = 0x811c9dc5;  // 2166136261

  // Hashing "test"
  const uint8_t data1[] = {'t', 'e', 's', 't'};
  uint32_t expectedTest = fnvBasis;
  for (uint8_t b : data1) {
    expectedTest ^= b;
    expectedTest *= 16777619u;
  }

  CHECK(fnv1aUpdate(fnvBasis, data1, sizeof(data1)) == expectedTest);

  // Same input is deterministic
  CHECK(fnv1aUpdate(fnvBasis, data1, sizeof(data1)) == fnv1aUpdate(fnvBasis, data1, sizeof(data1)));

  // 1-byte change changes the hash
  const uint8_t data2[] = {'t', 'e', 's', 'u'};
  CHECK(fnv1aUpdate(fnvBasis, data1, sizeof(data1)) != fnv1aUpdate(fnvBasis, data2, sizeof(data2)));
}

TEST_CASE("isFirmwareVersionChar: validates acceptable printable characters") {
  // True
  CHECK(isFirmwareVersionChar('1'));
  CHECK(isFirmwareVersionChar('.'));
  CHECK(isFirmwareVersionChar('A'));
  CHECK(isFirmwareVersionChar('~'));
  CHECK(isFirmwareVersionChar('!'));

  // False
  CHECK_FALSE(isFirmwareVersionChar('"'));
  CHECK_FALSE(isFirmwareVersionChar('\\'));
  CHECK_FALSE(isFirmwareVersionChar(' '));
  CHECK_FALSE(isFirmwareVersionChar('\n'));
  CHECK_FALSE(isFirmwareVersionChar('\0'));
}

TEST_CASE("advanceMarkerMatch: state machine accurately matches markers") {
  const char* marker = "abc";
  size_t matchLength = 0;

  // feed characters of marker "abc"
  advanceMarkerMatch('a', marker, matchLength);
  CHECK(matchLength == 1);
  advanceMarkerMatch('b', marker, matchLength);
  CHECK(matchLength == 2);
  advanceMarkerMatch('c', marker, matchLength);
  CHECK(matchLength == 3);

  // feed "abxabc"
  matchLength = 0;
  const char* input = "abxabc";
  for (size_t i = 0; input[i] != '\0'; ++i) {
    advanceMarkerMatch(input[i], marker, matchLength);
  }
  CHECK(matchLength == 3);
}

TEST_CASE("extractVersionAfterMarker: extracts string after marker correctly") {
  const char* marker = "Starting CrossPoint version ";

  {
    const uint8_t input[] = "...junk...Starting CrossPoint version 1.2.3\0more";
    std::string result = extractVersionAfterMarker(input, sizeof(input) - 1, marker);
    CHECK(result == "1.2.3");
  }

  {
    const uint8_t input[] = "buffer without the marker completely";
    std::string result = extractVersionAfterMarker(input, sizeof(input) - 1, marker);
    CHECK(result == "");
  }

  {
    const uint8_t input[] = "some junk before Starting CrossPoint version ";
    std::string result = extractVersionAfterMarker(input, sizeof(input) - 1, marker);
    CHECK(result == "");
  }
}

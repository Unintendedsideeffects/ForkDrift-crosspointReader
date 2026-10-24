#include <cstdint>
#include <optional>

#include "activities/reader/ProgressBytes.h"
#include "doctest/doctest.h"

TEST_CASE("progress.bin: 6-byte encode omits the content offset") {
  uint8_t data[10] = {};
  CHECK(EpubReaderUtils::encodeProgressBytes(data, sizeof(data), 3, 12, 40, std::nullopt) == 6);
  CHECK(data[0] == 3);
  CHECK(data[1] == 0);
  CHECK(data[2] == 12);
  CHECK(data[3] == 0);
  CHECK(data[4] == 40);
  CHECK(data[5] == 0);
}

TEST_CASE("progress.bin: 10-byte encode stores a little-endian offset") {
  uint8_t data[10] = {};
  CHECK(EpubReaderUtils::encodeProgressBytes(data, sizeof(data), 1, 2, 3, 0x04030201u) == 10);
  CHECK(data[6] == 0x01);
  CHECK(data[7] == 0x02);
  CHECK(data[8] == 0x03);
  CHECK(data[9] == 0x04);
}

TEST_CASE("progress.bin: decode accepts 4, 6, and 10 bytes") {
  int spine = -1;
  int page = -1;
  int count = -1;
  std::optional<uint32_t> offset;

  const uint8_t four[] = {7, 0, 9, 0};
  REQUIRE(EpubReaderUtils::decodeProgressBytes(four, sizeof(four), spine, page, count, offset));
  CHECK(spine == 7);
  CHECK(page == 9);
  CHECK(count == 0);
  CHECK_FALSE(offset.has_value());

  const uint8_t six[] = {1, 0, 2, 0, 8, 0};
  REQUIRE(EpubReaderUtils::decodeProgressBytes(six, sizeof(six), spine, page, count, offset));
  CHECK(spine == 1);
  CHECK(page == 2);
  CHECK(count == 8);
  CHECK_FALSE(offset.has_value());

  const uint8_t ten[] = {4, 0, 5, 0, 6, 0, 0x11, 0x22, 0x33, 0x44};
  REQUIRE(EpubReaderUtils::decodeProgressBytes(ten, sizeof(ten), spine, page, count, offset));
  CHECK(spine == 4);
  CHECK(page == 5);
  CHECK(count == 6);
  REQUIRE(offset.has_value());
  CHECK(*offset == 0x44332211u);
}

TEST_CASE("progress.bin: decode rejects other lengths") {
  int spine = 0;
  int page = 0;
  int count = 0;
  std::optional<uint32_t> offset;
  const uint8_t five[] = {1, 2, 3, 4, 5};
  CHECK_FALSE(EpubReaderUtils::decodeProgressBytes(five, sizeof(five), spine, page, count, offset));
  CHECK_FALSE(EpubReaderUtils::decodeProgressBytes(nullptr, 6, spine, page, count, offset));
}

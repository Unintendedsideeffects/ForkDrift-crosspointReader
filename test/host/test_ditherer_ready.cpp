#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "BitmapHelpers.h"
#include "PngToBmpConverter.h"
#include "doctest/doctest.h"
#include "test/mock/HalDisplay.h"
#include "test/mock/HalStorage.h"

namespace {

class VectorPrint : public Print {
 public:
  size_t write(uint8_t value) override {
    data.push_back(value);
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    data.insert(data.end(), buffer, buffer + size);
    return size;
  }
  std::vector<uint8_t> data;
};

HalFile fileFromBytes(const std::vector<uint8_t>& bytes) {
  auto buf = std::make_shared<std::vector<uint8_t>>(bytes);
  return HalFile::forFile("/tmp/ditherer.png", buf);
}

constexpr std::array<uint8_t, 69> kPng1BitGray4x2 = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x57, 0xd3, 0x40, 0xce, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x58, 0xc0, 0x10, 0x00, 0x00, 0x02, 0x34, 0x00, 0xf1, 0x28,
    0xf9, 0x61, 0x93, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

}  // namespace

TEST_CASE("ditherer isReady is false for non-positive width") {
  AtkinsonDitherer zero(0);
  CHECK_FALSE(zero.isReady());
  Atkinson1BitDitherer negative(-1);
  CHECK_FALSE(negative.isReady());
  FloydSteinbergDitherer fsZero(0);
  CHECK_FALSE(fsZero.isReady());
}

TEST_CASE("ditherer processPixel plus nextRow over three rows stays in slab") {
  AtkinsonDitherer atkinson(16);
  REQUIRE(atkinson.isReady());
  for (int row = 0; row < 3; ++row) {
    for (int x = 0; x < 16; ++x) {
      (void)atkinson.processPixel(128, x);
    }
    atkinson.nextRow();
  }

  Atkinson1BitDitherer oneBit(16);
  REQUIRE(oneBit.isReady());
  for (int row = 0; row < 3; ++row) {
    for (int x = 0; x < 16; ++x) {
      (void)oneBit.processPixel(128, x);
    }
    oneBit.nextRow();
  }

  FloydSteinbergDitherer fs(16);
  REQUIRE(fs.isReady());
  for (int row = 0; row < 3; ++row) {
    for (int x = 0; x < 16; ++x) {
      (void)fs.processPixel(128, x);
    }
    fs.nextRow();
  }
}

TEST_CASE("cover PNG convert succeeds when the ditherer is ready") {
  Storage.reset();
  const std::vector<uint8_t> bytes(kPng1BitGray4x2.begin(), kPng1BitGray4x2.end());
  HalFile pngFile = fileFromBytes(bytes);
  VectorPrint output;
  REQUIRE(PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, output, 4, 2, false));
  CHECK(output.data.size() > 62);
}

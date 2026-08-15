#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "InflateReader.h"
#include "InflateStream.h"
#include "PngToBmpConverter.h"
#include "doctest/doctest.h"
#include "test/mock/HalDisplay.h"
#include "test/mock/HalStorage.h"

HalDisplay display;

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

uint32_t readBe32(const uint8_t* bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

std::vector<uint8_t> extractIdatPayload(const std::vector<uint8_t>& png) {
  std::vector<uint8_t> idat;
  size_t pos = 8;
  while (pos + 12 <= png.size()) {
    const uint32_t chunkLen = readBe32(&png[pos]);
    pos += 4;
    const uint8_t* type = &png[pos];
    pos += 4;
    if (pos + chunkLen + 4 > png.size()) return {};
    if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), png.begin() + static_cast<std::ptrdiff_t>(pos),
                  png.begin() + static_cast<std::ptrdiff_t>(pos + chunkLen));
    }
    pos += chunkLen + 4;
    if (std::memcmp(type, "IEND", 4) == 0) break;
  }
  return idat;
}

std::vector<uint8_t> inflateWithUzlib(const std::vector<uint8_t>& idat, size_t outSize) {
  InflateReader reader;
  if (!reader.init(false)) return {};
  reader.setSource(idat.data(), idat.size());
  reader.skipZlibHeader();
  std::vector<uint8_t> out(outSize);
  if (!reader.read(out.data(), out.size())) return {};
  return out;
}

std::vector<uint8_t> inflateWithMiniz(const std::vector<uint8_t>& idat, size_t outSize) {
  InflateStream stream;
  if (!stream.init(false)) return {};
  stream.setSource(idat.data(), idat.size());
  stream.setZlibWrapped();
  std::vector<uint8_t> out(outSize);
  if (!stream.read(out.data(), out.size())) return {};
  return out;
}

std::vector<uint8_t> makeExpectedOnePixel1BitBmp() {
  std::vector<uint8_t> bmp(66, 0);
  bmp[0] = 'B';
  bmp[1] = 'M';
  bmp[2] = 66;
  bmp[10] = 62;
  bmp[14] = 40;
  bmp[18] = 1;
  bmp[22] = 0xFF;
  bmp[23] = 0xFF;
  bmp[24] = 0xFF;
  bmp[25] = 0xFF;
  bmp[26] = 1;
  bmp[28] = 1;
  bmp[34] = 4;
  bmp[38] = 0x13;
  bmp[39] = 0x0B;
  bmp[42] = 0x13;
  bmp[43] = 0x0B;
  bmp[46] = 2;
  bmp[50] = 2;
  bmp[58] = 0xFF;
  bmp[59] = 0xFF;
  bmp[60] = 0xFF;
  return bmp;
}

std::vector<uint8_t> fixturePng() {
  static constexpr std::array<uint8_t, 67> kPng = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3a, 0x7e, 0x9b, 0x55, 0x00,
      0x00, 0x00, 0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01,
      0x48, 0xaf, 0xa4, 0x71, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  return std::vector<uint8_t>(kPng.begin(), kPng.end());
}

}  // namespace

TEST_CASE("miniz inflate output matches uzlib output for PNG IDAT") {
  const std::vector<uint8_t> png = fixturePng();
  const std::vector<uint8_t> idat = extractIdatPayload(png);
  REQUIRE(idat.empty() == false);

  const std::vector<uint8_t> uzlibOut = inflateWithUzlib(idat, 2);
  const std::vector<uint8_t> minizOut = inflateWithMiniz(idat, 2);
  REQUIRE(uzlibOut.empty() == false);
  REQUIRE(minizOut.empty() == false);
  CHECK(minizOut == uzlibOut);
}

TEST_CASE("png to bmp conversion matches expected 1-bit bytes") {
  const std::vector<uint8_t> png = fixturePng();
  auto inputBuffer = std::make_shared<std::vector<uint8_t>>(png);
  HalFile pngFile = HalFile::forFile("/tmp/fixture.png", inputBuffer);
  VectorPrint output;

  const bool ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, output, 1, 1, false);
  REQUIRE(ok);

  const std::vector<uint8_t> expected = makeExpectedOnePixel1BitBmp();
  CHECK(output.data == expected);
}

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "PngToBmpConverter.h"
#include "doctest/doctest.h"
#include "test/mock/HalDisplay.h"
#include "test/mock/HalStorage.h"

// PngToBmpConverter is ForkDrift's miniz-backed PNG decoder (used for covers
// and other BMP-output paths) and is the only PNG decode path this suite can
// exercise end-to-end on the host: the EPUB in-book image path
// (PngToFramebufferConverter.cpp) decodes through PNGdec, an ESP32-only
// Arduino library not present on the host toolchain (see
// test/run_host_tests.sh's `lib_ignore = ... PNGdec` for the simulator env).
// PngToFramebufferConverter's row-expansion and row-repeat arithmetic is
// covered separately, standalone, in test_png_row_expansion_math.cpp.

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
  return HalFile::forFile("/tmp/fixture.png", buf);
}

// Decode a 1-bit BMP (as written by PngToBmpConverter::pngFileTo1BitBmpStreamWithSize)
// back into one uint8_t per pixel (0 or 255), row-major, so tests can assert
// on pixel values instead of hand-computed packed/padded BMP bytes.
std::vector<uint8_t> decode1BitBmpPixels(const std::vector<uint8_t>& bmp, int width, int height) {
  constexpr size_t kHeaderSize = 62;  // 14 (file) + 40 (info) + 8 (2-entry palette)
  const int bytesPerRow = ((width + 31) / 32) * 4;
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height, 0);
  REQUIRE(bmp.size() >= kHeaderSize + static_cast<size_t>(bytesPerRow) * height);
  for (int y = 0; y < height; y++) {
    const uint8_t* row = bmp.data() + kHeaderSize + static_cast<size_t>(y) * bytesPerRow;
    for (int x = 0; x < width; x++) {
      const int byteIndex = x / 8;
      const int bitOffset = 7 - (x % 8);
      const uint8_t bit = (row[byteIndex] >> bitOffset) & 1;
      pixels[static_cast<size_t>(y) * width + x] = bit ? 255 : 0;
    }
  }
  return pixels;
}

// 1-bit grayscale, 4x2. Row 0 sample bits [1,0,1,0] pack MSB-first into 0xA0;
// row 1 [0,1,0,1] into 0x50. Filter byte 0x00 (None) precedes each row.
constexpr std::array<uint8_t, 69> kPng1BitGray4x2 = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x57, 0xd3, 0x40,
    0xce, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x58, 0xc0, 0x10, 0x00,
    0x00, 0x02, 0x34, 0x00, 0xf1, 0x28, 0xf9, 0x61, 0x93, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82};

// 4-bit grayscale, 2x2. Row 0 samples [15,0] pack into 0xF0; row 1 [0,15] into 0x0F.
constexpr std::array<uint8_t, 69> kPng4BitGray2x2 = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2d, 0xbf,
    0xf9, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xc0, 0xc0, 0x0f,
    0x00, 0x02, 0xe3, 0x01, 0x00, 0xcd, 0xaf, 0x82, 0x91, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82};

// 8-bit grayscale, 1x2, samples [0, 255]: source rows for the upscale test.
constexpr std::array<uint8_t, 69> kPngGray1x2 = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0xbc, 0xea, 0xe9,
    0xfb, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0xf8, 0x0f,
    0x00, 0x01, 0x03, 0x01, 0x00, 0x36, 0x74, 0x11, 0x40, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
    0x44, 0xae, 0x42, 0x60, 0x82};

}  // namespace

TEST_CASE("1-bit grayscale PNG expands packed samples to the expected pixels") {
  HalFile pngFile = fileFromBytes({kPng1BitGray4x2.begin(), kPng1BitGray4x2.end()});
  VectorPrint output;

  // Target == source size: no scaling, isolates the bit-unpacking path.
  REQUIRE(PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, output, 4, 2, false));

  const std::vector<uint8_t> pixels = decode1BitBmpPixels(output.data, 4, 2);
  const std::vector<uint8_t> expected = {255, 0, 255, 0, 0, 255, 0, 255};
  CHECK(pixels == expected);
}

TEST_CASE("4-bit grayscale PNG expands packed samples to the expected pixels") {
  HalFile pngFile = fileFromBytes({kPng4BitGray2x2.begin(), kPng4BitGray2x2.end()});
  VectorPrint output;

  REQUIRE(PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, output, 2, 2, false));

  const std::vector<uint8_t> pixels = decode1BitBmpPixels(output.data, 2, 2);
  const std::vector<uint8_t> expected = {255, 0, 0, 255};
  CHECK(pixels == expected);
}

TEST_CASE("vertically upscaled PNG repeats source rows instead of leaving gaps") {
  HalFile pngFile = fileFromBytes({kPngGray1x2.begin(), kPngGray1x2.end()});
  VectorPrint output;

  // 1x2 source, crop=true -> isotropic scale = max(2/1, 4/2) = 2x -> 2x4 output.
  // Every one of the 4 output rows must be painted: row pairs [0,1] from the
  // black source row and [2,3] from the white one, not just rows 0 and 2.
  REQUIRE(PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, output, 2, 4, true));

  const std::vector<uint8_t> pixels = decode1BitBmpPixels(output.data, 2, 4);
  const std::vector<uint8_t> expected = {
      0,   0,    // row 0: from source row 0 (value 0)
      0,   0,    // row 1: repeated, not a zero-filled gap
      255, 255,  // row 2: from source row 1 (value 255)
      255, 255,  // row 3: repeated
  };
  CHECK(pixels == expected);
}

TEST_CASE("PNG decoder cleanly rejects SVG-wrapped content instead of misreading it as PNG") {
  // A raster <img> inside an EPUB can point at an SVG wrapper; if that file's
  // bytes ever reach the PNG decoder directly, the signature check must
  // reject them with a log rather than reading garbage as PNG chunks.
  const std::string svgMarkup =
      "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\"><image "
      "xlink:href=\"cover.png\"/></svg>";
  const std::vector<uint8_t> svgBytes(svgMarkup.begin(), svgMarkup.end());
  HalFile svgFile = fileFromBytes(svgBytes);
  VectorPrint output;

  CHECK_FALSE(PngToBmpConverter::pngFileToBmpStreamWithSize(svgFile, output, 100, 100, false));
  CHECK(output.data.empty());
}

TEST_CASE("PNG decoder cleanly rejects a truncated/empty file") {
  HalFile emptyFile = fileFromBytes({});
  VectorPrint output;
  CHECK_FALSE(PngToBmpConverter::pngFileToBmpStreamWithSize(emptyFile, output, 100, 100, false));
}

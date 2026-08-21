#include <cstdint>
#include <vector>

#include "PngRowExpansion.h"
#include "doctest/doctest.h"

// Pure PNG scanline arithmetic used by PngToFramebufferConverter's PNGdec draw
// callback. Tested directly against the PNG spec's bit-packing and sample
// scaling rules (7.2, 10.2) rather than through PNGdec, which is an ESP32-only
// Arduino library unavailable on the host toolchain.

TEST_CASE("packedRowBytes matches PNG spec ceil(width * bits / 8)") {
  CHECK(pngrow::packedRowBytes(8, 1) == 1);
  CHECK(pngrow::packedRowBytes(9, 1) == 2);   // one bit spills into a second byte
  CHECK(pngrow::packedRowBytes(4, 2) == 1);   // 4 samples * 2 bits = 8 bits exactly
  CHECK(pngrow::packedRowBytes(5, 2) == 2);   // 10 bits -> 2 bytes, 6 padding bits
  CHECK(pngrow::packedRowBytes(2, 4) == 1);   // 2 samples * 4 bits = 8 bits exactly
  CHECK(pngrow::packedRowBytes(3, 4) == 2);   // 12 bits -> 2 bytes
  CHECK(pngrow::packedRowBytes(10, 8) == 10);  // 8-bit samples: one byte each
}

TEST_CASE("readPackedSample unpacks MSB-first per PNG spec 7.2") {
  // 1-bit: byte 0b10110000 packs samples [1,0,1,1,0,0,0,0].
  const uint8_t oneBitRow[] = {0b10110000};
  const std::vector<uint8_t> expected1 = {1, 0, 1, 1, 0, 0, 0, 0};
  for (int x = 0; x < 8; x++) {
    CHECK(pngrow::readPackedSample(oneBitRow, x, 1) == expected1[x]);
  }

  // 2-bit: byte 0b11011000 packs samples [3,1,2,0].
  const uint8_t twoBitRow[] = {0b11011000};
  const std::vector<uint8_t> expected2 = {3, 1, 2, 0};
  for (int x = 0; x < 4; x++) {
    CHECK(pngrow::readPackedSample(twoBitRow, x, 2) == expected2[x]);
  }

  // 4-bit: byte 0 = 0xA7 packs samples [0xA, 0x7]; byte 1 = 0x30 packs [0x3, 0x0].
  const uint8_t fourBitRow[] = {0xA7, 0x30};
  CHECK(pngrow::readPackedSample(fourBitRow, 0, 4) == 0xA);
  CHECK(pngrow::readPackedSample(fourBitRow, 1, 4) == 0x7);
  CHECK(pngrow::readPackedSample(fourBitRow, 2, 4) == 0x3);
  CHECK(pngrow::readPackedSample(fourBitRow, 3, 4) == 0x0);

  // 8-bit: passthrough, one sample per byte.
  const uint8_t eightBitRow[] = {0x42, 0x99};
  CHECK(pngrow::readPackedSample(eightBitRow, 0, 8) == 0x42);
  CHECK(pngrow::readPackedSample(eightBitRow, 1, 8) == 0x99);
}

TEST_CASE("expandGraySampleToByte scales sample*255/maxSample per PNG spec 10.2") {
  // 1-bit: only 0 and 1 exist -> 0 and 255.
  CHECK(pngrow::expandGraySampleToByte(0, 1) == 0);
  CHECK(pngrow::expandGraySampleToByte(1, 1) == 255);

  // 2-bit: 0,1,2,3 -> 0,85,170,255.
  CHECK(pngrow::expandGraySampleToByte(0, 2) == 0);
  CHECK(pngrow::expandGraySampleToByte(1, 2) == 85);
  CHECK(pngrow::expandGraySampleToByte(2, 2) == 170);
  CHECK(pngrow::expandGraySampleToByte(3, 2) == 255);

  // 4-bit: 0,8,15 -> 0,136,255 (8*255/15 = 136).
  CHECK(pngrow::expandGraySampleToByte(0, 4) == 0);
  CHECK(pngrow::expandGraySampleToByte(8, 4) == 136);
  CHECK(pngrow::expandGraySampleToByte(15, 4) == 255);

  // 8-bit: passthrough.
  CHECK(pngrow::expandGraySampleToByte(200, 8) == 200);
}

TEST_CASE("isSupportedBitDepth allows sub-byte depths only for grayscale/indexed") {
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorGrayscale, 1));
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorGrayscale, 2));
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorGrayscale, 4));
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorGrayscale, 8));
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorIndexed, 1));
  CHECK(pngrow::isSupportedBitDepth(pngrow::kColorIndexed, 4));

  // Truecolor/truecolor-alpha/gray-alpha are always 8-bit per the PNG spec;
  // a sub-byte depth reported for them means something upstream is wrong.
  constexpr int kColorTruecolor = 2;
  CHECK_FALSE(pngrow::isSupportedBitDepth(kColorTruecolor, 4));
  CHECK(pngrow::isSupportedBitDepth(kColorTruecolor, 8));

  // 3-bit is not a legal PNG bit depth for any colour type.
  CHECK_FALSE(pngrow::isSupportedBitDepth(pngrow::kColorGrayscale, 3));
}

TEST_CASE("computeDstRowRange covers every output row exactly once on upscale") {
  // 2 source rows upscaled to 4 output rows: each source row must paint
  // exactly two output rows, with no gaps and no overlap.
  int lastPainted = -1;
  std::vector<int> paintedBy(4, -1);
  for (int srcY = 0; srcY < 2; srcY++) {
    int first, end;
    pngrow::computeDstRowRange(srcY, /*srcHeight=*/2, /*dstHeight=*/4, lastPainted, &first, &end);
    for (int y = first; y < end; y++) {
      REQUIRE(y >= 0);
      REQUIRE(y < 4);
      CHECK(paintedBy[y] == -1);  // no row painted twice
      paintedBy[y] = srcY;
      lastPainted = y;
    }
  }
  for (int y = 0; y < 4; y++) {
    CHECK(paintedBy[y] != -1);  // no row left unpainted
  }
  CHECK(paintedBy == std::vector<int>{0, 0, 1, 1});
}

TEST_CASE("computeDstRowRange downscale dedupes without repainting a row") {
  // 4 source rows downscaled to 2 output rows: total painted rows across all
  // calls must equal dstHeight, not srcHeight.
  int lastPainted = -1;
  int totalPaintedRows = 0;
  for (int srcY = 0; srcY < 4; srcY++) {
    int first, end;
    pngrow::computeDstRowRange(srcY, /*srcHeight=*/4, /*dstHeight=*/2, lastPainted, &first, &end);
    totalPaintedRows += (end - first);
    if (end > first) lastPainted = end - 1;
  }
  CHECK(totalPaintedRows == 2);
}

TEST_CASE("computeDstRowRange is a no-op guard against a zero source height") {
  int first, end;
  pngrow::computeDstRowRange(0, /*srcHeight=*/0, /*dstHeight=*/10, -1, &first, &end);
  CHECK(first == end);
}

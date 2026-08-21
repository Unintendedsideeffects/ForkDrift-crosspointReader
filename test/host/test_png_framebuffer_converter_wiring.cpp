#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "PngLineConversion.h"
#include "doctest/doctest.h"

// PngToFramebufferConverter.cpp's pngDrawCallback is PNGdec's draw callback
// for the EPUB in-book image path; it cannot be invoked directly on the host
// because it takes a PNGDRAW*, a type from PNGdec (an ESP32-only Arduino
// library not on the host toolchain -- see run_host_tests.sh's
// `lib_ignore = ... PNGdec` for the simulator env).
//
// convertLineToGray, requiredPngInternalBufferBytes, and prepareGrayLine
// (PngLineConversion.h) are the actual "wiring" between PNGdec's scanline
// fields and PngRowExpansion's row-repeat/bit-unpacking math -- and none of
// them take a PNGdec type, so they are called here directly with synthetic
// source buffers standing in for what PNGdec would have decoded. This is what
// the 452-case suite was missing: PngToBmpConverter (test_png_bit_depth_
// and_scaling.cpp) exercises a sibling decoder, not the code this callback
// actually runs. A wrong bitsPerSample, a swapped argument, or an off-by-one
// between a source width and a packed pitch would previously pass every host
// test; every expected value below is computed independently in this
// comment, not copied from the implementation.

TEST_CASE("bytesPerPixelFromType matches PNG spec 6.2 colour-type byte widths") {
  CHECK(pngrow::bytesPerPixelFromType(pngrow::kColorGrayscale) == 1);
  CHECK(pngrow::bytesPerPixelFromType(pngrow::kColorTruecolor) == 3);
  CHECK(pngrow::bytesPerPixelFromType(pngrow::kColorIndexed) == 1);
  CHECK(pngrow::bytesPerPixelFromType(pngrow::kColorGrayAlpha) == 2);
  CHECK(pngrow::bytesPerPixelFromType(pngrow::kColorTruecolorAlpha) == 4);
}

TEST_CASE("requiredPngInternalBufferBytes: srcWidth <= 0 is rejected") {
  CHECK(pngrow::requiredPngInternalBufferBytes(0, pngrow::kColorGrayscale, 8) == SIZE_MAX);
  CHECK(pngrow::requiredPngInternalBufferBytes(-1, pngrow::kColorGrayscale, 8) == SIZE_MAX);
}

TEST_CASE("requiredPngInternalBufferBytes: 8-bit grayscale uses one byte per pixel pitch") {
  // width=100, 1 byte/px -> pitch=100; (100+1)*2+32 = 234.
  CHECK(pngrow::requiredPngInternalBufferBytes(100, pngrow::kColorGrayscale, 8) == 234);
}

TEST_CASE("requiredPngInternalBufferBytes: sub-8-bit grayscale uses the packed pitch, not width") {
  // width=9, 1 bit/px -> packedRowBytes(9,1) = ceil(9/8) = 2; (2+1)*2+32 = 38.
  // A bug that used `width` directly as the pitch (9 instead of 2) would
  // compute (9+1)*2+32 = 52 here instead -- this pins the packed value.
  CHECK(pngrow::requiredPngInternalBufferBytes(9, pngrow::kColorGrayscale, 1) == 38);
}

TEST_CASE("requiredPngInternalBufferBytes: sub-8-bit indexed also uses the packed pitch") {
  // width=10, 4 bits/px -> packedRowBytes(10,4) = ceil(40/8) = 5; (5+1)*2+32 = 44.
  CHECK(pngrow::requiredPngInternalBufferBytes(10, pngrow::kColorIndexed, 4) == 44);
}

TEST_CASE("requiredPngInternalBufferBytes: truecolor uses 3 bytes per pixel") {
  // width=50, 3 bytes/px -> pitch=150; (150+1)*2+32 = 334.
  CHECK(pngrow::requiredPngInternalBufferBytes(50, pngrow::kColorTruecolor, 8) == 334);
}

TEST_CASE("requiredPngInternalBufferBytes: gray-alpha and truecolor-alpha use distinct byte widths") {
  // gray-alpha: width=10, 2 bytes/px -> pitch=20; (20+1)*2+32 = 74.
  CHECK(pngrow::requiredPngInternalBufferBytes(10, pngrow::kColorGrayAlpha, 8) == 74);
  // truecolor-alpha: width=10, 4 bytes/px -> pitch=40; (40+1)*2+32 = 114.
  // Same width as above, different pixel type -- a bytesPerPixelFromType
  // mix-up between the two 8-bit-only alpha formats would collide on 74.
  CHECK(pngrow::requiredPngInternalBufferBytes(10, pngrow::kColorTruecolorAlpha, 8) == 114);
}

TEST_CASE("requiredPngInternalBufferBytes: overflow-sized pitch is rejected, not wrapped") {
  // width=400000, 3 bytes/px -> pitch=1,200,000 > the 1,000,000 cap.
  CHECK(pngrow::requiredPngInternalBufferBytes(400000, pngrow::kColorTruecolor, 8) == SIZE_MAX);
}

TEST_CASE("convertLineToGray: 8-bit grayscale is a direct passthrough") {
  const uint8_t src[] = {10, 20, 30, 40};
  uint8_t out[4] = {0};
  pngrow::convertLineToGray(src, out, 4, pngrow::kColorGrayscale, 8, nullptr, 0);
  CHECK(std::vector<uint8_t>(out, out + 4) == std::vector<uint8_t>{10, 20, 30, 40});
}

TEST_CASE("convertLineToGray: 1-bit grayscale expands packed samples") {
  // 0b10110000 packs samples [1,0,1,1,0,0,0,0]; each scales by 255/1.
  const uint8_t src[] = {0b10110000};
  uint8_t out[8] = {0};
  pngrow::convertLineToGray(src, out, 8, pngrow::kColorGrayscale, 1, nullptr, 0);
  const std::vector<uint8_t> expected = {255, 0, 255, 255, 0, 0, 0, 0};
  CHECK(std::vector<uint8_t>(out, out + 8) == expected);
}

TEST_CASE("convertLineToGray: 4-bit grayscale expands packed samples") {
  // 0xF0 packs samples [15,0] -> 15*255/15=255, 0*255/15=0.
  const uint8_t src[] = {0xF0};
  uint8_t out[2] = {0};
  pngrow::convertLineToGray(src, out, 2, pngrow::kColorGrayscale, 4, nullptr, 0);
  CHECK(std::vector<uint8_t>(out, out + 2) == std::vector<uint8_t>{255, 0});
}

TEST_CASE("convertLineToGray: truecolor applies the 77/150/29 luma weights per channel") {
  // Pure R, pure G, pure B at full intensity isolate each coefficient and
  // catch a channel-order swap (e.g. reading pixels as B,G,R):
  //   R: (255*77) >> 8 = 19635 >> 8 = 76
  //   G: (255*150) >> 8 = 38250 >> 8 = 149
  //   B: (255*29) >> 8 = 7395 >> 8 = 28
  const uint8_t src[] = {255, 0, 0, 0, 255, 0, 0, 0, 255};
  uint8_t out[3] = {0};
  pngrow::convertLineToGray(src, out, 3, pngrow::kColorTruecolor, 8, nullptr, 0);
  CHECK(std::vector<uint8_t>(out, out + 3) == std::vector<uint8_t>{76, 149, 28});
}

TEST_CASE("convertLineToGray: indexed with an 8-bit palette resolves through RGB, not raw index") {
  // Raw indices are [0,1]; palette maps index 0 -> black (gray 0), index
  // 1 -> white (gray 255). A bug that read the palette as gray directly
  // rather than through the RGB triple would still get the right answer
  // here for pure black/white, so pixel-type 2 (below) uses non-gray colour.
  std::array<uint8_t, 1024> palette{};
  palette[0] = palette[1] = palette[2] = 0;      // index 0 -> black
  palette[3] = palette[4] = palette[5] = 255;    // index 1 -> white
  const uint8_t src[] = {0, 1};
  uint8_t out[2] = {0};
  pngrow::convertLineToGray(src, out, 2, pngrow::kColorIndexed, 8, palette.data(), 0);
  CHECK(std::vector<uint8_t>(out, out + 2) == std::vector<uint8_t>{0, 255});
}

TEST_CASE("convertLineToGray: sub-8-bit indexed reads packed indices before the palette lookup") {
  // 0b11011000 at 2 bits/sample packs indices [3,1,2,0] (same packing as
  // test_png_row_expansion_math.cpp's readPackedSample case). Palette: index
  // 0->black(0), 1->white(255), 2->pure red(76 after luma), 3->pure green(149).
  std::array<uint8_t, 1024> palette{};
  palette[0 * 3 + 0] = palette[0 * 3 + 1] = palette[0 * 3 + 2] = 0;
  palette[1 * 3 + 0] = palette[1 * 3 + 1] = palette[1 * 3 + 2] = 255;
  palette[2 * 3 + 0] = 255;
  palette[3 * 3 + 1] = 255;
  const uint8_t src[] = {0b11011000};
  uint8_t out[4] = {0};
  pngrow::convertLineToGray(src, out, 4, pngrow::kColorIndexed, 2, palette.data(), 0);
  CHECK(std::vector<uint8_t>(out, out + 4) == std::vector<uint8_t>{149, 255, 76, 0});
}

TEST_CASE("convertLineToGray: indexed with alpha blends the palette's alpha byte onto white") {
  // Index 0 -> black (gray 0); alpha stored at palette[768 + idx] = 128.
  // (0*128 + 255*(255-128)) / 255 = (0 + 32385) / 255 = 127 exactly.
  std::array<uint8_t, 1024> palette{};
  palette[0] = palette[1] = palette[2] = 0;
  palette[768] = 128;
  const uint8_t src[] = {0};
  uint8_t out[1] = {0};
  pngrow::convertLineToGray(src, out, 1, pngrow::kColorIndexed, 8, palette.data(), /*hasAlpha=*/1);
  CHECK(out[0] == 127);
}

TEST_CASE("convertLineToGray: indexed without a palette falls back to treating the index as gray") {
  // Spec-violating (indexed colour type, no PLTE) but must not crash --
  // pre-existing 8-bit fallback behaviour, extended to sub-8-bit here.
  const uint8_t src8[] = {5, 10, 15};
  uint8_t out8[3] = {0};
  pngrow::convertLineToGray(src8, out8, 3, pngrow::kColorIndexed, 8, nullptr, 0);
  CHECK(std::vector<uint8_t>(out8, out8 + 3) == std::vector<uint8_t>{5, 10, 15});

  const uint8_t src4[] = {0xF0};
  uint8_t out4[2] = {0};
  pngrow::convertLineToGray(src4, out4, 2, pngrow::kColorIndexed, 4, nullptr, 0);
  CHECK(std::vector<uint8_t>(out4, out4 + 2) == std::vector<uint8_t>{255, 0});
}

TEST_CASE("convertLineToGray: gray-alpha blends onto white by the alpha byte") {
  // (100*200 + 255*(255-200)) / 255 = (20000 + 14025) / 255 = 34025/255 = 133 (truncated).
  const uint8_t src[] = {100, 200};
  uint8_t out[1] = {0};
  pngrow::convertLineToGray(src, out, 1, pngrow::kColorGrayAlpha, 8, nullptr, 0);
  CHECK(out[0] == 133);
}

TEST_CASE("convertLineToGray: truecolor-alpha applies luma then blends by alpha") {
  // Pure red (gray=76, see luma test above) at alpha=128:
  // (76*128 + 255*(255-128)) / 255 = (9728 + 32385) / 255 = 42113/255 = 165 (truncated).
  const uint8_t src[] = {255, 0, 0, 128};
  uint8_t out[1] = {0};
  pngrow::convertLineToGray(src, out, 1, pngrow::kColorTruecolorAlpha, 8, nullptr, 0);
  CHECK(out[0] == 165);
}

TEST_CASE("convertLineToGray: unrecognized pixel type fills mid-gray instead of reading garbage") {
  const uint8_t src[] = {1, 2, 3, 4, 5};
  uint8_t out[5] = {0};
  pngrow::convertLineToGray(src, out, 5, /*pixelType=*/99, 8, nullptr, 0);
  CHECK(std::vector<uint8_t>(out, out + 5) == std::vector<uint8_t>{128, 128, 128, 128, 128});
}

TEST_CASE("prepareGrayLine: 1:1 scale converts the row and claims exactly one output row") {
  const uint8_t src[] = {0b10110000};
  uint8_t out[8];
  std::fill(std::begin(out), std::end(out), 0xAA);

  const pngrow::LineWriteRange range =
      pngrow::prepareGrayLine(/*srcY=*/0, /*srcWidth=*/8, /*srcHeight=*/1, /*dstHeight=*/1,
                              /*lastPaintedDstY=*/-1, src, pngrow::kColorGrayscale, 1, nullptr, 0, out);
  CHECK(range.firstDstY == 0);
  CHECK(range.endDstY == 1);
  const std::vector<uint8_t> expected = {255, 0, 255, 255, 0, 0, 0, 0};
  CHECK(std::vector<uint8_t>(out, out + 8) == expected);
}

TEST_CASE("prepareGrayLine: upscaling repeats a source row across the destination range it owns") {
  // Mirrors test_png_row_expansion_math.cpp's 2-source/4-dest upscale case:
  // srcY=0 must own [0,2), srcY=1 must own [2,4).
  const uint8_t rowA[] = {10, 20, 30};
  uint8_t out[3];
  pngrow::LineWriteRange range =
      pngrow::prepareGrayLine(/*srcY=*/0, /*srcWidth=*/3, /*srcHeight=*/2, /*dstHeight=*/4,
                              /*lastPaintedDstY=*/-1, rowA, pngrow::kColorGrayscale, 8, nullptr, 0, out);
  CHECK(range.firstDstY == 0);
  CHECK(range.endDstY == 2);
  CHECK(std::vector<uint8_t>(out, out + 3) == std::vector<uint8_t>{10, 20, 30});

  const uint8_t rowB[] = {40, 50, 60};
  range = pngrow::prepareGrayLine(/*srcY=*/1, /*srcWidth=*/3, /*srcHeight=*/2, /*dstHeight=*/4,
                                  /*lastPaintedDstY=*/1, rowB, pngrow::kColorGrayscale, 8, nullptr, 0, out);
  CHECK(range.firstDstY == 2);
  CHECK(range.endDstY == 4);
  CHECK(std::vector<uint8_t>(out, out + 3) == std::vector<uint8_t>{40, 50, 60});
}

TEST_CASE("prepareGrayLine: a downscaled row already covered by lastPaintedDstY skips the conversion") {
  // 4 source rows downscaled to 2 dest rows: srcY=1 maps to a row the
  // previous call already claimed, so the range must be empty and the
  // conversion must not run (the sentinel bytes must survive untouched).
  uint8_t out[3] = {0xAA, 0xAA, 0xAA};
  const uint8_t row[] = {1, 2, 3};
  const pngrow::LineWriteRange range =
      pngrow::prepareGrayLine(/*srcY=*/1, /*srcWidth=*/3, /*srcHeight=*/4, /*dstHeight=*/2,
                              /*lastPaintedDstY=*/0, row, pngrow::kColorGrayscale, 8, nullptr, 0, out);
  CHECK(range.firstDstY == range.endDstY);
  CHECK(std::vector<uint8_t>(out, out + 3) == std::vector<uint8_t>{0xAA, 0xAA, 0xAA});
}

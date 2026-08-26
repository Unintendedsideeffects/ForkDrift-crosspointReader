#include <cstdint>
#include <vector>

#include "ImageDimsProbe.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

// O'Reilly EPUBs (Site Reliability Engineering et al.) store ~98% of their
// figures as progressive-DCT JPEGs. JPEGDEC decodes only the first scan of such
// a file -- the DC coefficients, one flat average per 8x8 block -- and forces
// JPEG_SCALE_EIGHTH, so a nominal 900x584 figure really arrives as 112x73.
// Laying it out at the full container width stretched that DC grid ~3.6x and
// the 1-bit dither rendered the block edges as structured noise: the "garbled
// figures" defect. ChapterHtmlSlimParser now clamps such figures to 2x their
// decoded size, which requires the probe to report progressive-ness.

namespace {

// A JPEG header the probe can walk: SOI, an APP0 segment to prove segment
// skipping still works, then an SOFn frame header carrying the dimensions.
// `sofMarker` selects baseline (0xC0) vs progressive (0xC2).
std::vector<uint8_t> jpegHeader(const uint8_t sofMarker, const uint16_t width, const uint16_t height) {
  return {
      0xFF,
      0xD8,  // SOI
      0xFF,
      0xE0,
      0x00,
      0x04,
      0x00,
      0x00,  // APP0, length 4 (2 bytes of body)
      0xFF,
      sofMarker,
      0x00,
      0x11,
      0x08,  // SOFn, length 17, precision 8
      static_cast<uint8_t>(height >> 8),
      static_cast<uint8_t>(height & 0xFF),
      static_cast<uint8_t>(width >> 8),
      static_cast<uint8_t>(width & 0xFF),
      0x03,  // component count
  };
}

bool probeFile(const char* path, const std::vector<uint8_t>& bytes, ImageDimensions& dims, bool& progressive) {
  Storage.reset();
  HalFile writeHandle;
  Storage.openFileForWrite("TEST", path, writeHandle);
  writeHandle.write(bytes.data(), bytes.size());
  return imagedims::probeFromFile(path, dims, &progressive);
}

// The clamp ChapterHtmlSlimParser applies once layout has resolved a box.
// Mirrored here so the arithmetic is pinned independently of the parser's
// XML-driven call path, which the host build cannot reach.
void clampProgressive(const int srcWidth, int& displayWidth, int& displayHeight) {
  const int dcWidth = srcWidth / 8;
  const int capWidth = dcWidth * 2;
  if (capWidth > 0 && displayWidth > capWidth) {
    const int cappedHeight = static_cast<int>((static_cast<int64_t>(displayHeight) * capWidth) / displayWidth);
    if (cappedHeight > 0) {
      displayWidth = capWidth;
      displayHeight = cappedHeight;
    }
  }
}

}  // namespace

TEST_CASE("probe reports a progressive SOF2 frame as progressive") {
  ImageDimensions dims{0, 0};
  bool progressive = false;
  REQUIRE(probeFile("/cache/fig.jpg", jpegHeader(0xC2, 900, 584), dims, progressive));
  CHECK(dims.width == 900);
  CHECK(dims.height == 584);
  CHECK(progressive == true);
}

TEST_CASE("probe reports a baseline SOF0 frame as not progressive") {
  ImageDimensions dims{0, 0};
  bool progressive = true;  // seeded wrong on purpose
  REQUIRE(probeFile("/cache/fig.jpg", jpegHeader(0xC0, 900, 584), dims, progressive));
  CHECK(dims.width == 900);
  CHECK(progressive == false);
}

TEST_CASE("probe leaves progressive false for a PNG") {
  // PNG has no SOF marker at all; the flag must not be left indeterminate.
  const std::vector<uint8_t> png = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,  //
                                    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,  //
                                    0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0xc8};
  ImageDimensions dims{0, 0};
  bool progressive = true;
  REQUIRE(probeFile("/cache/fig.png", png, dims, progressive));
  CHECK(progressive == false);
}

TEST_CASE("a failed probe does not report progressive") {
  const std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03};
  ImageDimensions dims{0, 0};
  bool progressive = true;
  CHECK_FALSE(probeFile("/cache/fig.bin", garbage, dims, progressive));
  CHECK(progressive == false);
}

TEST_CASE("progressive clamp caps a full-width figure at 2x its decoded size") {
  // The real SRE case: 900x584 laid out to a 400px container.
  int w = 400, h = 259;
  clampProgressive(900, w, h);
  CHECK(w == 224);  // (900/8) * 2
  CHECK(h == 145);  // aspect ratio preserved
}

TEST_CASE("progressive clamp leaves a figure already within the cap alone") {
  // A small figure the container never had to stretch.
  int w = 100, h = 60;
  clampProgressive(900, w, h);
  CHECK(w == 100);
  CHECK(h == 60);
}

TEST_CASE("progressive clamp never produces a zero-height figure") {
  // A 1px-tall rule laid out full width. The cap (800/8)*2 = 200 does apply, but
  // scaling height by 200/400 rounds to 0. Rather than emit an invisible element
  // the clamp must decline and leave the box as laid out.
  int w = 400, h = 1;
  clampProgressive(800, w, h);
  CHECK(w == 400);
  CHECK(h == 1);
}

TEST_CASE("progressive clamp is a no-op when the cap exceeds the laid-out box") {
  // A large source whose 2x-DC cap is wider than the container: nothing to do.
  int w = 400, h = 300;
  clampProgressive(4000, w, h);  // cap = (4000/8)*2 = 1000 > 400
  CHECK(w == 400);
  CHECK(h == 300);
}

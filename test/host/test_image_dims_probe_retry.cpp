#include <cstdint>
#include <vector>

#include "ImageDimsProbe.h"
#include "doctest/doctest.h"
#include "test/mock/HalStorage.h"

// imagedims::probeFromFile() is called immediately after a fresh cache-file
// extraction (open-for-write, flush, close) by both SleepActivity and
// ChapterHtmlSlimParser (via probeImageDimensions). On a slow SD card the
// data may not be visible to a subsequent open-for-read yet, so a single
// failure there must not be fatal -- but the retry must fire ONLY once the
// first attempt has already failed. A blanket retry (or a blanket delay
// before the first attempt) would slow down every already-synced or
// genuinely-unsupported-format probe for no reason.

namespace {

// Minimal 24-byte PNG header ImageDimsProbe reads dimensions from: signature
// (8) + IHDR chunk length (4, unchecked by the probe) + "IHDR" (4) +
// width=100 BE (4) + height=200 BE (4). No CRC or IDAT needed -- the probe
// only reads the fixed-offset IHDR fields.
std::vector<uint8_t> validPngHeader() {
  return {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,  // signature
          0x00, 0x00, 0x00, 0x0d,                          // IHDR length (unchecked)
          0x49, 0x48, 0x44, 0x52,                          // "IHDR"
          0x00, 0x00, 0x00, 0x64,                          // width = 100
          0x00, 0x00, 0x00, 0xc8};                         // height = 200
}

}  // namespace

TEST_CASE("probeFromFile does not retry when the first attempt succeeds") {
  Storage.reset();
  HalFile writeHandle;
  Storage.openFileForWrite("TEST", "/cache/cover.png", writeHandle);
  const std::vector<uint8_t> header = validPngHeader();
  writeHandle.write(header.data(), header.size());

  ImageDimensions dims{0, 0};
  REQUIRE(imagedims::probeFromFile("/cache/cover.png", dims));
  CHECK(dims.width == 100);
  CHECK(dims.height == 200);
  CHECK(Storage.openFileForReadCount() == 1);  // no blanket retry on success
}

TEST_CASE("probeFromFile retries only after a failed attempt, and recovers") {
  Storage.reset();
  HalFile writeHandle;
  Storage.openFileForWrite("TEST", "/cache/slow.png", writeHandle);
  const std::vector<uint8_t> header = validPngHeader();
  writeHandle.write(header.data(), header.size());

  // Simulate a slow SD card: the file exists, but the first open-for-read
  // (as if issued right after the write) fails as if the card had not
  // finished syncing yet. The second attempt must succeed.
  Storage.failNextReads(1);

  ImageDimensions dims{0, 0};
  REQUIRE(imagedims::probeFromFile("/cache/slow.png", dims));
  CHECK(dims.width == 100);
  CHECK(dims.height == 200);
  CHECK(Storage.openFileForReadCount() == 2);  // exactly one retry, not more
}

TEST_CASE("probeFromFile gives up after bounded retries on a persistently missing file") {
  Storage.reset();
  // No file written at all: openFileForRead always fails.

  ImageDimensions dims{0, 0};
  CHECK_FALSE(imagedims::probeFromFile("/cache/missing.png", dims));
  // Bounded: must eventually give up rather than retrying forever.
  CHECK(Storage.openFileForReadCount() > 1);
  CHECK(Storage.openFileForReadCount() <= 5);
}

TEST_CASE("probeFromFile rejects an unsupported header without hanging") {
  Storage.reset();
  HalFile writeHandle;
  Storage.openFileForWrite("TEST", "/cache/notimage.bmp", writeHandle);
  const std::vector<uint8_t> notAnImage = {'B', 'M', 0, 0, 0, 0};  // BMP magic; probe only knows PNG/JPEG
  writeHandle.write(notAnImage.data(), notAnImage.size());

  ImageDimensions dims{0, 0};
  CHECK_FALSE(imagedims::probeFromFile("/cache/notimage.bmp", dims));
}

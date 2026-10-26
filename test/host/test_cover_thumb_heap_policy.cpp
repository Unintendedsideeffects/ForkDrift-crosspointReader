#include "doctest/doctest.h"
#include "util/CoverThumbHeapPolicy.h"

TEST_CASE("inline first-page bake needs the reserved window, the decoder run, and the JPEG working set") {
  const uint32_t jpegNeed = CoverThumbHeapPolicy::kJpegWorkingSetBytes;
  const uint32_t decoder = CoverThumbHeapPolicy::kJpegDecoderBytes;

  CHECK_FALSE(CoverThumbHeapPolicy::canBakeInline({jpegNeed, decoder, false}));
  CHECK_FALSE(CoverThumbHeapPolicy::canBakeInline({jpegNeed - 1, decoder, true}));
  CHECK_FALSE(CoverThumbHeapPolicy::canBakeInline({jpegNeed, decoder - 1, true}));
  // A reader-typical 13 KB largest block cannot hold the decoder.
  CHECK_FALSE(CoverThumbHeapPolicy::canBakeInline({60000, 13300, true}));
  // Device walk 2026-10-26: a first-page open with the radio off left a 20,468-byte
  // run (a 20 KB block minus the 12-byte header). sizeof(JPEGDEC) is 17,884.
  CHECK(CoverThumbHeapPolicy::canBakeInline({75084, 20468, true}));
  CHECK(CoverThumbHeapPolicy::canBakeInline({jpegNeed, decoder, true}));
}

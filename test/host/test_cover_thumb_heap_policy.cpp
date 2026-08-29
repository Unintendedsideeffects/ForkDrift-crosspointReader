#include "doctest/doctest.h"
#include "util/CoverThumbHeapPolicy.h"

TEST_CASE("cover thumbs skip when the inflate window is not reserved") {
  const uint32_t jpegNeed = CoverThumbHeapPolicy::kJpegWorkingSetBytes;
  const uint32_t window = CoverThumbHeapPolicy::kInflateWindowBytes;

  CHECK(jpegNeed == 53248);
  CHECK_FALSE(CoverThumbHeapPolicy::canAttempt({39000, 15860, false}));
  CHECK_FALSE(CoverThumbHeapPolicy::canAttempt({window + jpegNeed - 1, window * 2, false}));
  CHECK_FALSE(CoverThumbHeapPolicy::canAttempt({window + jpegNeed, window * 2 - 1, false}));
  CHECK(CoverThumbHeapPolicy::canAttempt({window + jpegNeed, window * 2, false}));
}

TEST_CASE("cover thumbs skip a reserved window that still cannot hold JPEG work") {
  const uint32_t jpegNeed = CoverThumbHeapPolicy::kJpegWorkingSetBytes;
  const uint32_t window = CoverThumbHeapPolicy::kInflateWindowBytes;

  CHECK_FALSE(CoverThumbHeapPolicy::canAttempt({jpegNeed - 1, window, true}));
  CHECK_FALSE(CoverThumbHeapPolicy::canAttempt({jpegNeed, window - 1, true}));
  CHECK(CoverThumbHeapPolicy::canAttempt({jpegNeed, window, true}));
}

#include <cstddef>
#include <cstdint>
#include <limits>

#include "activities/reader/ReaderOptionsMemoryPolicy.h"
#include "doctest/doctest.h"

namespace {

constexpr uint32_t kBuildTotal = ReaderOptionsMemoryPolicy::kReserveTotalBytes;
constexpr uint32_t kBuildLargest = ReaderOptionsMemoryPolicy::kReserveLargestBlock;
constexpr uint32_t kX4FrameBufferBytes = 48000;
constexpr uint32_t kX3FrameBufferBytes = 52272;

}  // namespace

TEST_CASE("ReaderOptionsMemoryPolicy X4 preview boundaries") {
  constexpr uint32_t kRetainTotal = 144000;
  constexpr uint32_t kRetainLargest = 96000;

  CHECK(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal, kRetainLargest}, kX4FrameBufferBytes));
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal - 1, kRetainLargest}, kX4FrameBufferBytes));
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal, kRetainLargest - 1}, kX4FrameBufferBytes));
}

TEST_CASE("ReaderOptionsMemoryPolicy X3 preview boundaries") {
  constexpr uint32_t kRetainTotal = 148272;
  constexpr uint32_t kRetainLargest = 100272;

  CHECK(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal, kRetainLargest}, kX3FrameBufferBytes));
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal - 1, kRetainLargest}, kX3FrameBufferBytes));
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview({kRetainTotal, kRetainLargest - 1}, kX3FrameBufferBytes));
}

TEST_CASE("ReaderOptionsMemoryPolicy rejects framebuffer arithmetic overflow") {
  constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
  const ReaderMemorySnapshot maximumHeap{kMax, kMax};
  const size_t exceedsTotal = static_cast<size_t>(kMax) - kBuildTotal + 1U;
  const size_t exceedsLargest = static_cast<size_t>(kMax) - kBuildLargest + 1U;

  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview(maximumHeap, exceedsTotal));
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview(maximumHeap, exceedsLargest));
}

TEST_CASE("measured reader heap cannot retain a 48 KB preview clone") {
  CHECK_FALSE(ReaderOptionsMemoryPolicy::canRetainPreview({42000, 15000}, kX4FrameBufferBytes));
}

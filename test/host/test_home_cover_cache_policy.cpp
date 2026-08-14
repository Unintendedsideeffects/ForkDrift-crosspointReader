#include <cstddef>
#include <cstdint>
#include <limits>

#include "activities/home/HomeCoverCachePolicy.h"
#include "doctest/doctest.h"

namespace {

constexpr size_t kX4FrameBufferBytes = 48000;
constexpr size_t kX3FrameBufferBytes = 52272;
constexpr uint32_t kFloor = HomeCoverCachePolicy::kFloorAfterBytes;
constexpr uint32_t kPlentyLargest = std::numeric_limits<uint32_t>::max() / 2;

}  // namespace

TEST_CASE("HomeCoverCachePolicy free-heap boundary") {
  // Exactly enough to take the buffer and still clear the floor.
  CHECK(HomeCoverCachePolicy::canStore({kX4FrameBufferBytes + kFloor, kPlentyLargest}, kX4FrameBufferBytes));
  CHECK_FALSE(HomeCoverCachePolicy::canStore({kX4FrameBufferBytes + kFloor - 1, kPlentyLargest}, kX4FrameBufferBytes));

  CHECK(HomeCoverCachePolicy::canStore({kX3FrameBufferBytes + kFloor, kPlentyLargest}, kX3FrameBufferBytes));
  CHECK_FALSE(HomeCoverCachePolicy::canStore({kX3FrameBufferBytes + kFloor - 1, kPlentyLargest}, kX3FrameBufferBytes));
}

TEST_CASE("HomeCoverCachePolicy requires one contiguous run") {
  // The failure mode the PNG-sleep-image finding recorded: plenty of total heap,
  // no single block big enough to hold the request.
  const HomeCoverCacheMemory fragmented{kX4FrameBufferBytes + kFloor, kX4FrameBufferBytes - 1};
  CHECK_FALSE(HomeCoverCachePolicy::canStore(fragmented, kX4FrameBufferBytes));

  const HomeCoverCacheMemory contiguous{kX4FrameBufferBytes + kFloor, kX4FrameBufferBytes};
  CHECK(HomeCoverCachePolicy::canStore(contiguous, kX4FrameBufferBytes));
}

TEST_CASE("HomeCoverCachePolicy refuses the measured X4 low-heap snapshots") {
  // Regression: these are the two live CMD:HEAPTRACE samples from an X4 booting
  // to Home with the background WiFi server resident. Both were accepted by the
  // old bare malloc, and the second left the device at free=15240 largest=9204 --
  // far below heapguard's 32KB critical floor. Neither may ever be admitted
  // again.
  CHECK_FALSE(HomeCoverCachePolicy::canStore({81884, 65524}, kX4FrameBufferBytes));
  CHECK_FALSE(HomeCoverCachePolicy::canStore({63256, 49140}, kX4FrameBufferBytes));

  // A device with the WiFi stack down still has room, so the cache is not dead
  // code -- it is conditioned on actually being affordable.
  CHECK(HomeCoverCachePolicy::canStore({133584, 118772}, kX4FrameBufferBytes));
}

TEST_CASE("HomeCoverCachePolicy rejects degenerate inputs without underflowing") {
  CHECK_FALSE(HomeCoverCachePolicy::canStore({0, 0}, kX4FrameBufferBytes));
  CHECK_FALSE(HomeCoverCachePolicy::canStore({kPlentyLargest, kPlentyLargest}, 0));

  // freeHeap < bufferBytes must not wrap around into a pass.
  CHECK_FALSE(HomeCoverCachePolicy::canStore({1, kPlentyLargest}, kX4FrameBufferBytes));
  CHECK_FALSE(HomeCoverCachePolicy::canStore({kPlentyLargest, kPlentyLargest}, std::numeric_limits<size_t>::max()));
}

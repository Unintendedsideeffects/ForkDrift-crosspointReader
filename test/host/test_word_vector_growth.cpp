#include <cstddef>
#include <string>

#include "Epub/WordVectorGrowth.h"
#include "doctest/doctest.h"

namespace {
constexpr size_t kCriticalFloor = 32 * 1024;  // heapguard::kCriticalFloorBytes

// The effective free-heap threshold a request implies, since
// heapguard::canAllocate(bytes) fails when free - bytes < floor.
constexpr size_t thresholdFor(const size_t requestBytes) { return requestBytes + kCriticalFloor; }
}  // namespace

TEST_CASE("nextCapacity follows geometric growth with a floor") {
  CHECK(wordgrowth::nextCapacity(0, 1) == wordgrowth::kMinCapacity);
  CHECK(wordgrowth::nextCapacity(16, 17) == 32);
  CHECK(wordgrowth::nextCapacity(32, 33) == 64);
  // A single word that splits into many tokens must be satisfied in one step
  // rather than doubling repeatedly.
  CHECK(wordgrowth::nextCapacity(32, 500) == 500);
  // Already sufficient: unchanged, so growthBytes() can report "no allocation".
  CHECK(wordgrowth::nextCapacity(64, 10) == 64);
}

TEST_CASE("growthBytes is zero when the existing capacity suffices") {
  CHECK(wordgrowth::growthBytes(64, 10) == 0);
  CHECK(wordgrowth::growthBytes(64, 64) == 0);
  CHECK(wordgrowth::growthBytes(64, 65) > 0);
}

TEST_CASE("requestBytes charges the string allowance when no growth is due") {
  // Appending into spare capacity allocates no vector storage, so the request
  // must fall back to the word's own bytes rather than to a flat constant.
  const size_t request = wordgrowth::requestBytes(/*capacity=*/64, /*required=*/10, /*wordBytes=*/12);
  CHECK(request == wordgrowth::kStringSlackBytes + 24);
  CHECK(request < 1024);
}

TEST_CASE("requestBytes charges the real growth when a reallocation is due") {
  const size_t request = wordgrowth::requestBytes(/*capacity=*/32, /*required=*/33, /*wordBytes=*/8);
  CHECK(request == 64 * wordgrowth::kBytesPerSlot);
}

// The regression this whole header exists for. The old guard asked for a flat
// 8KB on every word, which on top of the 32KB critical floor refused to append
// text while ~41KB was still free. Device-measured: the guard tripped at
// free=38156 with largest=14836, blanking whole chapters.
TEST_CASE("the measured device snapshot must no longer refuse text") {
  constexpr size_t kMeasuredFreeHeap = 38156;
  constexpr size_t kOldFlatRequest = 8 * 1024;

  // What used to happen, and must not happen again.
  CHECK(thresholdFor(kOldFlatRequest) > kMeasuredFreeHeap);

  // Steady-state paragraph, spare capacity available: a short word costs a
  // couple of hundred bytes, not 8KB.
  CHECK(thresholdFor(wordgrowth::requestBytes(64, 40, 10)) < kMeasuredFreeHeap);

  // A growth step mid-paragraph also clears it.
  CHECK(thresholdFor(wordgrowth::requestBytes(64, 65, 10)) < kMeasuredFreeHeap);

  // Honest limit, asserted rather than glossed: the 128->256 step does NOT fit
  // at this snapshot. A paragraph past ~128 tokens still truncates at 38KB free.
  // That is a real and much rarer cliff than "every word, always", and pinning
  // it here means the next person sees it instead of rediscovering it on
  // hardware.
  CHECK(thresholdFor(wordgrowth::requestBytes(128, 129, 10)) > kMeasuredFreeHeap);
}

TEST_CASE("a pathological growth is still refused rather than waved through") {
  // The point is to size the check honestly, not to disable it: a request that
  // genuinely would cross the floor must still fail.
  const size_t huge = wordgrowth::requestBytes(4096, 8192, 32);
  CHECK(huge > 64 * 1024);
  CHECK(thresholdFor(huge) > 38156);
}

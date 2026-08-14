#pragma once

#include <HeapGuard.h>

#include <cstddef>
#include <cstdint>

// Admission policy for HomeActivity's cover buffer.
//
// The cover buffer is a full framebuffer copy (48,000 bytes on X4, 52,272 on
// X3) kept so Home navigation can restore the rendered cover instead of
// re-reading cover BMPs from SD. It is a pure optimisation: declining it costs a
// slower redraw and nothing else, which is why HeapReclaimRegistry is already
// allowed to drop it under pressure.
//
// It used to be a bare malloc with no heap check at all, and that made it the
// single largest driver of the device's since-boot low-water mark. Measured on
// an X4 (CMD:HEAPTRACE, boot to Home):
//
//   home:covbuf-pre   free=81884  min=78940
//   home:covbuf-post  free=33868  min=33868   <- one 48,000-byte malloc
//   home:covbuf-pre   free=63256  min=12092
//   home:covbuf-post  free=15240  min=12092   largest=9204
//
// i.e. it walked straight through heapguard's 32KB critical floor and left the
// device holding 9KB of contiguous heap, purely to cache a redraw.
//
// The floor is therefore kLowFloorBytes, not kCriticalFloorBytes. HeapGuard.h
// defines LOW as the level at which to "defer optional luxuries (previews,
// covers, prefetch)" -- this buffer is exactly that, so it may only be taken
// while the device stays out of LOW pressure *afterwards*. On a device with the
// WiFi stack resident (~50KB) that means the cache is skipped; with WiFi down
// there is room and it is still taken.
//
// Kept as a pure function of a snapshot, rather than a direct
// heapguard::canAllocate() call inside storeCoverBuffer(), so the boundaries are
// host-testable -- same shape as ReaderOptionsMemoryPolicy.
struct HomeCoverCacheMemory {
  uint32_t freeHeap;
  uint32_t largestBlock;
};

struct HomeCoverCachePolicy {
  // Free heap that must remain after the buffer is taken.
  static constexpr size_t kFloorAfterBytes = heapguard::kLowFloorBytes;

  static bool canStore(const HomeCoverCacheMemory& snapshot, const size_t bufferBytes) {
    if (bufferBytes == 0) {
      return false;
    }
    // A 48KB request can fail with far more than 48KB "free" once the heap is
    // fragmented, so the contiguous run is checked independently of the total.
    if (bufferBytes > snapshot.largestBlock) {
      return false;
    }
    // Ordered to avoid the underflow that a plain
    // `freeHeap - bufferBytes >= floor` would hit on a low-heap snapshot.
    if (bufferBytes > snapshot.freeHeap) {
      return false;
    }
    return snapshot.freeHeap - bufferBytes >= kFloorAfterBytes;
  }
};

#pragma once

#include <cstddef>
#include <cstdint>

struct ReaderMemorySnapshot {
  uint32_t freeHeap;
  uint32_t maxAllocHeap;
};

class ReaderOptionsMemoryPolicy {
 public:
  static constexpr uint32_t kReserveTotalBytes = 96000;
  static constexpr uint32_t kReserveLargestBlock = 48000;

  static bool canRetainPreview(const ReaderMemorySnapshot& snapshot, size_t frameBufferSize) {
    if (frameBufferSize > UINT32_MAX - kReserveTotalBytes || frameBufferSize > UINT32_MAX - kReserveLargestBlock) {
      return false;
    }

    const uint32_t requiredTotal = kReserveTotalBytes + static_cast<uint32_t>(frameBufferSize);
    const uint32_t requiredLargest = kReserveLargestBlock + static_cast<uint32_t>(frameBufferSize);

    return snapshot.freeHeap >= requiredTotal && snapshot.maxAllocHeap >= requiredLargest;
  }
};

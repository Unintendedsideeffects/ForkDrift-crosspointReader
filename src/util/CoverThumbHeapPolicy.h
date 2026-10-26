#pragma once

#include <cstdint>

// Cover thumbs are luxury. They must not take the only 32 KB inflate run
// book-open still needs, and must not punch a 32,768-byte hole that later
// cannot be reacquired. Skip unless the shared window is already reserved, or
// there is room to pin it plus a second ring and the JPEG working set
// (20 KB decoder + 32 KB = 53248, matching JpegToBmpConverter).

struct CoverThumbMemory {
  uint32_t freeHeap = 0;
  uint32_t largestBlock = 0;
  bool inflateWindowReserved = false;
};

struct CoverThumbHeapPolicy {
  static constexpr uint32_t kInflateWindowBytes = 32768;
  static constexpr uint32_t kJpegWorkingSetBytes = 20 * 1024 + 32 * 1024;
  // JPEGDEC is one contiguous object: sizeof(JPEGDEC) = 17,884 on riscv32
  // (measured 2026-10-26). JpegToBmpConverter's JPEG_DECODER_SIZE (20 KB) is a
  // rounded estimate, and gating on it missed a real 20,468-byte run -- every
  // reported run is 12 bytes under its nominal size (allocator header).
  static constexpr uint32_t kJpegDecoderBytes = 18 * 1024;

  // The reader's first-page bake runs inline under the RenderLock, so nothing else
  // inflates concurrently and the cover extract reuses the reserved window. What is
  // left is the converter's own requirement: the decoder object plus its working set.
  static bool canBakeInline(const CoverThumbMemory& snapshot) {
    return snapshot.inflateWindowReserved && snapshot.largestBlock >= kJpegDecoderBytes &&
           snapshot.freeHeap >= kJpegWorkingSetBytes;
  }

  static bool canAttempt(const CoverThumbMemory& snapshot) {
    if (snapshot.largestBlock < kInflateWindowBytes) {
      return false;
    }
    if (!snapshot.inflateWindowReserved) {
      if (snapshot.largestBlock < kInflateWindowBytes * 2u) {
        return false;
      }
      if (snapshot.freeHeap < kInflateWindowBytes) {
        return false;
      }
      return snapshot.freeHeap - kInflateWindowBytes >= kJpegWorkingSetBytes;
    }
    return snapshot.freeHeap >= kJpegWorkingSetBytes;
  }
};

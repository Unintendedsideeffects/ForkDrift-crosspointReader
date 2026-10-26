#pragma once

#include <cstdint>

// Cover thumbs run only with the shared inflate window already reserved: inside
// a reader (inline, under the RenderLock) or with the window taken for the bake
// (arrival, boot catch-up). Nothing else inflates concurrently, so the cover
// extract reuses that window and never needs a second 32 KB run.

struct CoverThumbMemory {
  uint32_t freeHeap = 0;
  uint32_t largestBlock = 0;
  bool inflateWindowReserved = false;
};

struct CoverThumbHeapPolicy {
  // JpegToBmpConverter's MIN_FREE_HEAP: decoder + 32 KB of row/scale buffers.
  static constexpr uint32_t kJpegWorkingSetBytes = 20 * 1024 + 32 * 1024;
  // JPEGDEC is one contiguous object: sizeof(JPEGDEC) = 17,884 on riscv32
  // (measured 2026-10-26). JpegToBmpConverter's JPEG_DECODER_SIZE (20 KB) is a
  // rounded estimate, and gating on it missed a real 20,468-byte run -- every
  // reported run is 12 bytes under its nominal size (allocator header).
  static constexpr uint32_t kJpegDecoderBytes = 18 * 1024;

  static bool canBakeInline(const CoverThumbMemory& snapshot) {
    return snapshot.inflateWindowReserved && snapshot.largestBlock >= kJpegDecoderBytes &&
           snapshot.freeHeap >= kJpegWorkingSetBytes;
  }
};

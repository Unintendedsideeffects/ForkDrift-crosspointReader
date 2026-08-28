#include "InflateReader.h"

#include <HeapGuard.h>
#include <Logging.h>

#include <cstring>
#include <type_traits>

namespace {
constexpr size_t INFLATE_DICT_SIZE = 32768;

// Shared, persistent DEFLATE window. uzlib needs a 32KB dictionary for streaming
// inflate. Allocating (and freeing) it per call fragments an already-tight heap
// and then fails when no 32KB-contiguous block remains — the failure mode that
// left image-heavy EPUBs unable to extract figures. Inflate is serial on this
// single-core device, so one buffer, allocated once and reused, is both frugal
// (no per-call churn) and reliable (no repeated fragmented allocation). Kept for
// the process lifetime once claimed; a rare re-entrant inflate falls back to a
// private malloc.
uint8_t* g_sharedWindow = nullptr;
bool g_sharedWindowInUse = false;
}  // namespace

// Guarantee the cast pattern in the header comment is valid.
static_assert(std::is_standard_layout<InflateReader>::value,
              "InflateReader must be standard-layout for the uzlib callback cast to work");

InflateReader::~InflateReader() { deinit(); }

bool InflateReader::init(const bool streaming) {
  deinit();  // free any previously allocated ring buffer and reset state

  if (streaming) {
    // Prefer the shared window: allocate it once (lazily), then reuse it for
    // every subsequent inflate instead of re-allocating a fresh 32KB block.
    if (!g_sharedWindowInUse) {
      if (g_sharedWindow == nullptr) {
        // Lazy claim: ensureSharedWindow() either was not called or failed. Log it,
        // otherwise a failed reservation followed by a successful claim here reads as
        // a permanent failure in the log and misleads the analysis. This is also the
        // worse path -- claiming mid-session, from a working heap, measured a ~11.7 KB
        // steady largest block against ~20.5 KB when claimed at open.
        const size_t largestBefore = heapguard::largestBlock();
        g_sharedWindow = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
        LOG_INF("INF", "Inflate window claimed lazily (largest block was %u): %s", (unsigned)largestBefore,
                g_sharedWindow != nullptr ? "ok" : "FAILED, falling back to a private buffer");
      }
      if (g_sharedWindow != nullptr) {
        ringBuffer = g_sharedWindow;
        usesSharedWindow = true;
        g_sharedWindowInUse = true;
      }
    }
    // Re-entrant inflate (shared window busy) or first allocation failed: private buffer.
    if (ringBuffer == nullptr) {
      ringBuffer = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
      if (!ringBuffer) return false;
    }
    memset(ringBuffer, 0, INFLATE_DICT_SIZE);
  }

  uzlib_uncompress_init(&decomp, ringBuffer, ringBuffer ? INFLATE_DICT_SIZE : 0);
  return true;
}

void InflateReader::releaseSharedWindow() {
  if (g_sharedWindow == nullptr || g_sharedWindowInUse) {
    return;
  }
  free(g_sharedWindow);
  g_sharedWindow = nullptr;
}

bool InflateReader::ensureSharedWindow() {
  if (g_sharedWindow != nullptr) {
    return true;  // already allocated
  }
  // Log the contiguous run we are asking against, on success as well as failure.
  // This reservation fails intermittently and silently, and the consequence shows up
  // much later and looks unrelated: a book without its window cannot inflate, so its
  // next uncached chapter cannot be indexed. Measured 2026-08-28, one observed
  // failure had a 31,732-byte largest block against the 32,768 required -- short by
  // 1,036. Recording the margin every time is what tells us whether that was a
  // near-miss or a rout, which decides whether this is worth tuning or has to be
  // designed away. See docs/heap/architecture-verdict.md.
  const size_t largestBefore = heapguard::largestBlock();
  g_sharedWindow = static_cast<uint8_t*>(malloc(INFLATE_DICT_SIZE));
  if (g_sharedWindow == nullptr) {
    LOG_ERR("INF", "Failed to pre-allocate inflate window (%u bytes): largest block %u, short by %u",
            (unsigned)INFLATE_DICT_SIZE, (unsigned)largestBefore,
            (unsigned)(largestBefore < INFLATE_DICT_SIZE ? INFLATE_DICT_SIZE - largestBefore : 0));
    return false;
  }
  LOG_INF("INF", "Inflate window reserved: largest block was %u, margin %u", (unsigned)largestBefore,
          (unsigned)(largestBefore > INFLATE_DICT_SIZE ? largestBefore - INFLATE_DICT_SIZE : 0));
  return true;
}

void InflateReader::deinit() {
  if (ringBuffer) {
    if (usesSharedWindow) {
      g_sharedWindowInUse = false;  // release for reuse; keep g_sharedWindow allocated
    } else {
      free(ringBuffer);
    }
    ringBuffer = nullptr;
  }
  usesSharedWindow = false;
  memset(&decomp, 0, sizeof(decomp));
}

void InflateReader::setSource(const uint8_t* src, size_t len) {
  decomp.source = src;
  decomp.source_limit = src + len;
}

void InflateReader::setReadCallback(int (*cb)(struct uzlib_uncomp*)) { decomp.source_read_cb = cb; }

void InflateReader::skipZlibHeader() {
  uzlib_get_byte(&decomp);
  uzlib_get_byte(&decomp);
}

bool InflateReader::read(uint8_t* dest, size_t len) {
  if (!ringBuffer) {
    // One-shot mode: back-references use absolute offset from dest_start.
    // Valid only when read() is called once with the full output buffer.
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + len;

  const int res = uzlib_uncompress(&decomp);
  if (res < 0) return false;
  return decomp.dest == decomp.dest_limit;
}

InflateStatus InflateReader::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  if (!ringBuffer) {
    // One-shot mode: back-references use absolute offset from dest_start.
    // Valid only when readAtMost() is called once with the full output buffer.
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + maxLen;

  const int res = uzlib_uncompress(&decomp);
  *produced = static_cast<size_t>(decomp.dest - dest);

  if (res == TINF_DONE) return InflateStatus::Done;
  if (res < 0) return InflateStatus::Error;
  return InflateStatus::Ok;
}

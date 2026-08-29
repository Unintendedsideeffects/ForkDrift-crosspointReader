#pragma once

#include <cstdint>

// Post-index recovery. The section cache is already on SD; a silent reboot is
// not a legal action. Reclaim caches → covers → HTTP keep-Wi-Fi, then continue.
// The 14 KB floor is the lower measured successful post-index largest block
// (2026-08-29: 14,836 and 16,372). After kMaxReclaimAttempts, StopRetrying.

namespace reader_heap_recovery {

enum class AfterIndexAction : uint8_t {
  Continue,
  Reclaim,
  StopRetrying,
};

constexpr uint32_t kReclaimBelowLargestBlockBytes = 14 * 1024;
constexpr uint8_t kMaxReclaimAttempts = 2;

constexpr AfterIndexAction decideAfterIndex(const bool sectionAvailable, const uint32_t largestBlockBytes,
                                            const uint8_t reclaimAttemptsAlready) {
  if (!sectionAvailable) {
    return AfterIndexAction::Continue;
  }
  if (largestBlockBytes >= kReclaimBelowLargestBlockBytes) {
    return AfterIndexAction::Continue;
  }
  if (reclaimAttemptsAlready >= kMaxReclaimAttempts) {
    return AfterIndexAction::StopRetrying;
  }
  return AfterIndexAction::Reclaim;
}

}  // namespace reader_heap_recovery

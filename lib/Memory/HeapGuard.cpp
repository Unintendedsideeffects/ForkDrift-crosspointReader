#include "HeapGuard.h"

#include <Logging.h>

#if defined(SIMULATOR) || defined(CROSSPOINT_HOST_BUILD)
#include <Arduino.h>  // ESPMock: sim_heap.cpp budget tracker
#else
#include <esp_heap_caps.h>
#include <esp_system.h>
#endif

// Work around ESP32 HAL macro conflict with enum values
#ifdef LOW
#undef LOW
#endif
#ifdef HIGH
#undef HIGH
#endif

namespace heapguard {

size_t freeBytes() {
#if defined(SIMULATOR) || defined(CROSSPOINT_HOST_BUILD)
  return ESP.getFreeHeap();
#else
  return esp_get_free_heap_size();
#endif
}

size_t largestBlock() {
#if defined(SIMULATOR) || defined(CROSSPOINT_HOST_BUILD)
  return ESP.getMaxAllocHeap();
#else
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
}

#if defined(SIMULATOR) || defined(CROSSPOINT_HOST_BUILD)
// The ESPMock budget tracker models totals, not a block layout, so there is no
// honest number to return here. 0 reads as "unknown" at every call site rather
// than inventing a fragmentation figure the simulator cannot know.
size_t freeBlockCount() { return 0; }
size_t allocatedBlockCount() { return 0; }
#else
namespace {
multi_heap_info_t heapInfo() {
  multi_heap_info_t info{};
  heap_caps_get_info(&info, MALLOC_CAP_8BIT);
  return info;
}
}  // namespace

size_t freeBlockCount() { return heapInfo().free_blocks; }
size_t allocatedBlockCount() { return heapInfo().allocated_blocks; }
#endif

Pressure pressure() {
  const size_t free = freeBytes();
  if (free < kCriticalFloorBytes) {
    return Pressure::Critical;
  }
  if (free < kLowFloorBytes) {
    return Pressure::Low;
  }
  return Pressure::Normal;
}

bool canAllocate(const size_t bytes, const size_t floorAfter) {
  const size_t free = freeBytes();
  if (free < bytes || free - bytes < floorAfter) {
    return false;
  }
  return bytes <= largestBlock();
}

void logState(const char* tag) {
  LOG_INF("HEAP", "%s: free=%u largest=%u", tag, static_cast<unsigned>(freeBytes()),
          static_cast<unsigned>(largestBlock()));
}

}  // namespace heapguard

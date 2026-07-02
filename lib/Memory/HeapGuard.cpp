#include "HeapGuard.h"

#include <Logging.h>

#ifdef SIMULATOR
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
#ifdef SIMULATOR
  return ESP.getFreeHeap();
#else
  return esp_get_free_heap_size();
#endif
}

size_t largestBlock() {
#ifdef SIMULATOR
  return freeBytes();  // sim heap does not model fragmentation
#else
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
}

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

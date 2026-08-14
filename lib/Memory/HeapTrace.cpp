#include "HeapTrace.h"

#include <Arduino.h>  // millis(); on sim/host also the ESPMock heap budget tracker
#include <Logging.h>

#include "HeapGuard.h"

#if !defined(SIMULATOR) && !defined(CROSSPOINT_HOST_BUILD)
#include <esp_system.h>
#endif

namespace heaptrace {
namespace {

Mark marks[kCapacity];
size_t markCount = 0;
size_t droppedCount = 0;

size_t minFreeBytes() {
#if defined(SIMULATOR) || defined(CROSSPOINT_HOST_BUILD)
  return ESP.getMinFreeHeap();
#else
  return esp_get_minimum_free_heap_size();
#endif
}

}  // namespace

void mark(const char* label) {
  if (markCount >= kCapacity) {
    droppedCount++;
    return;
  }
  marks[markCount] = Mark{label, static_cast<uint32_t>(::millis()), static_cast<uint32_t>(heapguard::freeBytes()),
                          static_cast<uint32_t>(minFreeBytes()), static_cast<uint32_t>(heapguard::largestBlock())};
  markCount++;
}

size_t count() { return markCount; }

const Mark& at(const size_t index) { return marks[index]; }

bool overflowed() { return droppedCount > 0; }

void dump(const char* tag) {
  LOG_INF(tag, "heaptrace: %u marks%s", static_cast<unsigned>(markCount), overflowed() ? " (TRUNCATED)" : "");
  uint32_t previousMin = 0;
  for (size_t i = 0; i < markCount; i++) {
    const Mark& m = marks[i];
    // The delta on the low-water mark is the whole point: the step that prints a
    // non-zero drop is the step that consumed the heap.
    const uint32_t minDrop = (i == 0 || previousMin <= m.minFreeBytes) ? 0 : previousMin - m.minFreeBytes;
    LOG_INF(tag, "heaptrace[%u] t=%u %s free=%u min=%u (-%u) largest=%u", static_cast<unsigned>(i),
            static_cast<unsigned>(m.millis), m.label, static_cast<unsigned>(m.freeBytes),
            static_cast<unsigned>(m.minFreeBytes), static_cast<unsigned>(minDrop),
            static_cast<unsigned>(m.largestBlock));
    previousMin = m.minFreeBytes;
  }
}

}  // namespace heaptrace

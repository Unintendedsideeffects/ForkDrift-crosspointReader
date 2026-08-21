#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "String.h"

#ifndef PROGMEM
#define PROGMEM
#endif

struct MockESP {
  size_t overrideFreeHeap = 1024 * 1024;
  size_t overrideMaxAllocHeap = 1024 * 1024;
  size_t getFreeHeap() { return overrideFreeHeap; }
  size_t getMaxAllocHeap() { return overrideMaxAllocHeap; }
  void reset() {
    overrideFreeHeap = 1024 * 1024;
    overrideMaxAllocHeap = 1024 * 1024;
  }
};
extern MockESP ESP;
inline unsigned long mockMillisVal = 0;
inline unsigned long millis() { return mockMillisVal; }
// Host tests must run instantly; production delay() is intentionally not
// simulated with a real sleep. Retry-after-failure logic (e.g.
// imagedims::probeFromFile) is verified via attempt counts, not wall-clock time.
inline void delay(unsigned long /*ms*/) {}

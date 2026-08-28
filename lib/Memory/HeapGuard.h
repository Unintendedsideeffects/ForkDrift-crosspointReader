#pragma once

#include <cstddef>
#include <cstdint>

// Central heap-pressure policy for the 380KB-RAM ESP32-C3.
//
// Every feature that makes a large or failure-prone allocation should consult
// this instead of hand-rolling its own free-heap check, so thresholds live in
// one place and all features degrade consistently:
//
//   if (!heapguard::canAllocate(bufSize)) {         // leaves CRITICAL floor intact
//     LOG_ERR("MOD", "skipping X: low heap");
//     return false;                                  // degrade, don't crash
//   }
//   auto buf = makeUniqueNoThrow<uint8_t[]>(bufSize);  // still null-check!
//
// canAllocate() is a pre-flight heuristic, not a reservation: allocations can
// still fail (fragmentation, races), so the nothrow null-check remains
// mandatory. It also checks the largest free block, because on this heap a
// 48KB request can fail with 100KB "free" once fragmented.
//
// Works on device (esp_get_free_heap_size / heap_caps) and in the simulator
// (sim_heap total and largest-block budgets via ESP.getFreeHeap()/getMaxAllocHeap()).
namespace heapguard {

// PascalCase enumerators: NORMAL/LOW/HIGH collide with Arduino GPIO macros.
enum class Pressure : uint8_t {
  Normal = 0,
  Low = 1,      // defer optional luxuries (previews, covers, prefetch)
  Critical = 2  // only essential allocations; heavy features must refuse
};

// Floors tuned against observed steady-state reading heap (~60-130KB free;
// see "MEM enter/exit" logs). LOW leaves room for a 48KB framebuffer-sized
// buffer plus slack; CRITICAL is the do-not-cross line for system stability.
constexpr size_t kLowFloorBytes = 60 * 1024;
constexpr size_t kCriticalFloorBytes = 32 * 1024;

// Current free heap in bytes.
size_t freeBytes();

// Largest single allocatable block (fragmentation-aware on device; independently
// capped in the simulator through SIM_HEAP_LARGEST).
size_t largestBlock();

// Heap block counts, for telling fragmentation apart from genuine exhaustion.
// freeBytes() alone cannot: 44KB free with a 12KB largest block and 44KB free
// with a 44KB largest block are the same number and completely different
// situations. A free-block count that climbs while freeBytes() stays flat is
// fragmentation happening in front of you.
//
// These walk the heap under its lock, so they cost more than freeBytes(). Call
// them at state transitions and on request, not in a render loop.
// Return 0 in the simulator/host build, which has no block model.
size_t freeBlockCount();
size_t allocatedBlockCount();

// Current pressure level from freeBytes() vs the floors above.
Pressure pressure();

// True if allocating `bytes` now would (a) fit in the largest free block and
// (b) leave at least `floorAfter` bytes of free heap afterwards.
bool canAllocate(size_t bytes, size_t floorAfter = kCriticalFloorBytes);

// LOG_INF one line of heap state, tagged with the calling feature. Use around
// heavy operations so OOM field reports carry the numbers we need.
void logState(const char* tag);

}  // namespace heapguard

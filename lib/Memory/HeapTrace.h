#pragma once

#include <cstddef>
#include <cstdint>

// A tiny in-RAM record of heap state at named milestones, readable after the
// fact.
//
// Why this exists rather than a LOG_INF at each milestone: the X4's serial is
// native USB-CDC carried over USB/IP, and every device reset drops the host
// attach. Re-attaching takes longer than the whole boot, so nothing logged
// before roughly t+20s is ever seen by the host. Meanwhile
// `ESP.getMinFreeHeap()` is a monotonic since-boot low-water record — once it
// has dropped there is no way to learn *when* it dropped by watching a running
// device. Marks are therefore recorded into RAM during boot and dumped later on
// request (`CMD:HEAPTRACE`).
//
// Cost: kCapacity * sizeof(Mark) bytes of static DRAM (400 bytes at 20 x 20).
// That is deliberate and permanent — boot-time heap forensics are not
// reproducible any other way on this hardware.
//
// Usage:
//   heaptrace::mark("setup:display");   // label must be a string literal
//   heaptrace::dump("MAIN");            // LOG_INF one line per mark
namespace heaptrace {

constexpr size_t kCapacity = 20;

struct Mark {
  const char* label;  // borrowed; must outlive the trace (string literal)
  uint32_t millis;    // uptime at the mark; without it a gap between two marks
                      // cannot be told from a long-running step
  uint32_t freeBytes;
  uint32_t minFreeBytes;
  uint32_t largestBlock;
};

// Record heap state under `label`. Silently ignored once kCapacity marks have
// been taken — the trace is a boot record, not a ring, so the earliest marks
// (the ones that explain the low-water mark) are never overwritten.
void mark(const char* label);

// Number of marks recorded (never exceeds kCapacity).
size_t count();

// Mark at `index`; index must be < count().
const Mark& at(size_t index);

// True if mark() was called more times than kCapacity, i.e. marks were dropped.
bool overflowed();

// LOG_INF one line per recorded mark, tagged with `tag`.
void dump(const char* tag);

}  // namespace heaptrace

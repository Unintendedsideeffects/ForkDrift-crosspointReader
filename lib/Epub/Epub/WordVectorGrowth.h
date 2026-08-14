#pragma once

#include <cstddef>
#include <string>

// Growth arithmetic for ParsedText's five parallel word vectors.
//
// Split out as a dependency-free header so the numbers are host-testable: the
// old guard was a flat 8KB constant that nobody could exercise, and it turned
// out to be the reason whole chapters laid out to nothing on device.
//
// Why this exists at all: std::vector growth that cannot allocate throws
// bad_alloc, which under -fno-exceptions is terminate(). So every append has to
// be preceded by a heap check. The question is only how big that check should
// be, and the answer used to be "8KB, always" -- which, on top of heapguard's
// 32KB critical floor, refused to append a single word while ~41KB was still
// free. Device-measured: the guard tripped at free=38156 with largest=14836, an
// 8KB request that would have fit the largest block four times over.
namespace wordgrowth {

// The five vectors reserve together, so charge them together. `words` dominates
// at sizeof(std::string) per slot (24 bytes on this 32-bit target); wordStyles
// adds one byte and the three std::vector<bool> about one bit each, rounded up
// to one byte between them.
// (24 on the 32-bit device target, 32 on a 64-bit host; deriving it from
// sizeof rather than hardcoding keeps the host test honest about the target it
// is actually running on.)
constexpr size_t kBytesPerSlot = sizeof(std::string) + 2;

// Floor on the first allocation, so early words do not trigger a run of tiny
// reallocations. Matches the pre-existing focus-reading reserve.
constexpr size_t kMinCapacity = 16;

// The capacity std::vector would end up with to hold `requiredSlots`, following
// the same geometric growth the focus-reading path already assumed. Returns the
// current capacity unchanged when it is already sufficient.
constexpr size_t nextCapacity(const size_t currentCapacity, const size_t requiredSlots) {
  if (currentCapacity >= requiredSlots) {
    return currentCapacity;
  }
  size_t grown = currentCapacity * 2;
  if (grown < requiredSlots) {
    grown = requiredSlots;
  }
  if (grown < kMinCapacity) {
    grown = kMinCapacity;
  }
  return grown;
}

// Bytes the next capacity step would request, or 0 when the existing capacity
// already covers `requiredSlots` and therefore nothing is allocated.
constexpr size_t growthBytes(const size_t currentCapacity, const size_t requiredSlots) {
  const size_t grown = nextCapacity(currentCapacity, requiredSlots);
  return grown == currentCapacity ? 0 : grown * kBytesPerSlot;
}

// What to charge the heap check for one addWord() call.
//
// `wordBytes` covers the word's own string buffer: SSO overflow, the NFC
// composition copy, and the CJK/focus substrings, which together stay within
// about twice the input plus a fixed allowance. It is charged even when no
// vector growth is due, because those small allocations can still fail — but
// charging ~128 bytes instead of 8KB is the whole point.
constexpr size_t kStringSlackBytes = 128;

constexpr size_t requestBytes(const size_t currentCapacity, const size_t requiredSlots, const size_t wordBytes) {
  const size_t growth = growthBytes(currentCapacity, requiredSlots);
  const size_t strings = wordBytes * 2 + kStringSlackBytes;
  return growth > strings ? growth : strings;
}

}  // namespace wordgrowth

#pragma once

#include <cstddef>

// Admission policy for the per-chapter footnote link list.
//
// Extracted as a pure predicate because ChapterHtmlSlimParser.cpp pulls in expat and
// GfxRenderer and so cannot be built for the host test suite, while this decision is
// exactly the part that has to stay correct: a wrong answer here is not a mis-rendered
// footnote, it is an abort().
//
// Background: std::vector growth allocates the new block while the old one is still
// live, and under -fno-exceptions a failed allocation calls terminate() rather than
// returning an error. Observed on device (2026-08-12) as abort() inside endElement()'s
// push_back on a footnote-dense technical book, with the largest free block at 15348
// bytes. See docs/FINDINGS.md.
namespace footnote_guard {

// True when another footnote entry may be recorded.
//
//   size            - entries already recorded
//   capacity        - the vector's current capacity (growth is only needed at size ==
//                     capacity; below that push_back cannot allocate and cannot throw)
//   maxEntries      - hard per-chapter cap, bounding the worst case independently of heap
//   heapHeadroomOk  - result of heapguard::canAllocate() for a growth-sized block
//
// Entries are dropped rather than truncating the chapter: losing a tappable footnote link
// degrades the page, losing the device loses the reader's place.
constexpr bool canAccept(size_t size, size_t capacity, size_t maxEntries, bool heapHeadroomOk) {
  if (size >= maxEntries) return false;
  const bool needsGrowth = size == capacity;
  return !needsGrowth || heapHeadroomOk;
}

}  // namespace footnote_guard

#pragma once

#include <cstdint>

namespace section_cache {

enum class BuiltSectionAction : uint8_t {
  Cache,
  RefuseTruncated,
};

// A chapter is cached only if every word made it into the layout. ParsedText's
// heap guard drops words when an append cannot be made safely; a section built
// through that is missing text, and once on SD every later open reads it back
// instead of re-indexing. Missing words are never acceptable (user decision
// 2026-10-26), so any dropped word refuses the cache; the reader then reclaims
// heap and rebuilds rather than showing a lossy chapter.
constexpr BuiltSectionAction evaluateBuiltSection(const uint32_t heapTruncationTally) {
  return heapTruncationTally == 0 ? BuiltSectionAction::Cache : BuiltSectionAction::RefuseTruncated;
}

}  // namespace section_cache

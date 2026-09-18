#pragma once

#include <cstddef>
#include <cstdint>

namespace section_cache {

enum class EmptySectionAction : uint8_t {
  Cache,
  RefuseOccupancy,
};

struct EmptySectionInput {
  size_t totalPageElements = 0;
  uint32_t heapTruncationTally = 0;
  int imagesAttempted = 0;
};

constexpr EmptySectionAction evaluateEmptySection(const EmptySectionInput& input) {
  if (input.totalPageElements > 0) {
    return EmptySectionAction::Cache;
  }
  if (input.heapTruncationTally == 0) {
    return EmptySectionAction::Cache;
  }
  if (input.imagesAttempted > 0) {
    return EmptySectionAction::Cache;
  }
  return EmptySectionAction::RefuseOccupancy;
}

}  // namespace section_cache

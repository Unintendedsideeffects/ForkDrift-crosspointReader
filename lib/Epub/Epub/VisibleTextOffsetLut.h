#pragma once

#include <cstdint>
#include <optional>

namespace visible_text_lut {

inline std::optional<uint16_t> pageForOffset(const uint32_t* pageStarts, const uint16_t count, const uint32_t offset,
                                             const bool preferFirstAtOffset) {
  if (pageStarts == nullptr || count == 0) {
    return std::nullopt;
  }
  uint16_t result = 0;
  for (uint16_t i = 0; i < count; i++) {
    const uint32_t pageStart = pageStarts[i];
    if (preferFirstAtOffset && pageStart == offset) {
      return i;
    }
    if (pageStart > offset) {
      break;
    }
    result = i;
  }
  return result;
}

}  // namespace visible_text_lut

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace EpubReaderUtils {

inline size_t encodeProgressBytes(uint8_t* data, size_t cap, int spineIndex, int pageNumber, int pageCount,
                                  std::optional<uint32_t> visibleTextOffset) {
  if (data == nullptr || cap < 6) {
    return 0;
  }
  data[0] = static_cast<uint8_t>(spineIndex & 0xFF);
  data[1] = static_cast<uint8_t>((spineIndex >> 8) & 0xFF);
  data[2] = static_cast<uint8_t>(pageNumber & 0xFF);
  data[3] = static_cast<uint8_t>((pageNumber >> 8) & 0xFF);
  data[4] = static_cast<uint8_t>(pageCount & 0xFF);
  data[5] = static_cast<uint8_t>((pageCount >> 8) & 0xFF);
  if (!visibleTextOffset.has_value()) {
    return 6;
  }
  if (cap < 10) {
    return 0;
  }
  data[6] = static_cast<uint8_t>(*visibleTextOffset & 0xFF);
  data[7] = static_cast<uint8_t>((*visibleTextOffset >> 8) & 0xFF);
  data[8] = static_cast<uint8_t>((*visibleTextOffset >> 16) & 0xFF);
  data[9] = static_cast<uint8_t>((*visibleTextOffset >> 24) & 0xFF);
  return 10;
}

inline bool decodeProgressBytes(const uint8_t* data, size_t len, int& spineIndex, int& pageNumber, int& pageCount,
                                std::optional<uint32_t>& visibleTextOffset) {
  if (data == nullptr || (len != 4 && len != 6 && len != 10)) {
    return false;
  }
  spineIndex = data[0] + (data[1] << 8);
  pageNumber = data[2] + (data[3] << 8);
  pageCount = 0;
  visibleTextOffset.reset();
  if (len >= 6) {
    pageCount = data[4] + (data[5] << 8);
  }
  if (len == 10) {
    visibleTextOffset = static_cast<uint32_t>(data[6]) | (static_cast<uint32_t>(data[7]) << 8) |
                        (static_cast<uint32_t>(data[8]) << 16) | (static_cast<uint32_t>(data[9]) << 24);
  }
  return true;
}

}  // namespace EpubReaderUtils

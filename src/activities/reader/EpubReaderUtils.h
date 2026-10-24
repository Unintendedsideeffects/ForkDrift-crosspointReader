#pragma once

#include <Epub.h>
#include <Logging.h>

#include <optional>

#include "ProgressBytes.h"
#include "ProgressFile.h"

namespace EpubReaderUtils {

inline bool saveProgress(const Epub& epub, int spineIndex, int pageNumber, int pageCount,
                         std::optional<uint32_t> visibleTextOffset = std::nullopt) {
  if (spineIndex < 0 || spineIndex > 0xFFFF || pageNumber < 0 || pageNumber > 0xFFFF || pageCount < 0 ||
      pageCount > 0xFFFF) {
    LOG_ERR("ERS", "Progress values out of range: spine=%d page=%d count=%d", spineIndex, pageNumber, pageCount);
    return false;
  }
  uint8_t data[10];
  const size_t dataSize = encodeProgressBytes(data, sizeof(data), spineIndex, pageNumber, pageCount, visibleTextOffset);
  if (dataSize == 0 || !ProgressFile::writeAtomic(epub.getCachePath(), data, dataSize)) {
    return false;
  }
  LOG_DBG("ERS", "Progress saved: spine=%d offset=%u page=%d", spineIndex, visibleTextOffset.value_or(0), pageNumber);
  return true;
}

}  // namespace EpubReaderUtils

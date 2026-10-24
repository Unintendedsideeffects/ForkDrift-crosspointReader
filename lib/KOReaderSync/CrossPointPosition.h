#pragma once

#include <cstdint>

struct CrossPointPosition {
  int spineIndex;
  int pageNumber;
  int totalPages;
  uint32_t visibleTextOffset = 0;
  bool hasVisibleTextOffset = false;
  uint16_t paragraphIndex = 0;
  bool hasParagraphIndex = false;
  uint16_t liIndex = 0;
  bool hasLiIndex = false;
  char xpathAnchorId[64] = {};
  bool hasResolvedSpineIndex = false;
  bool hasMappedPage = false;
};

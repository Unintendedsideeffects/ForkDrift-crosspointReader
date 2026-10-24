#pragma once

#include <cmath>

inline bool koSyncPercentageUsable(const bool isNumeric, const float percentage) {
  return isNumeric && std::isfinite(percentage) && percentage >= 0.f && percentage <= 1.f;
}

inline bool koSyncHasStoredProgress(const bool progressNull, const bool percentageNumeric, const float percentage) {
  if (!progressNull) {
    return true;
  }
  return koSyncPercentageUsable(percentageNumeric, percentage);
}

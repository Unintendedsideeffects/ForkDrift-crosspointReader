#include "BookReadingStats.h"

#include <I18n.h>

#include "ReadingStatsStore.h"

BookReadingStats BookReadingStats::load(const std::string& cachePath) {
  ReadingStatsStore::getInstance().ensureLoaded();
  return ReadingStatsStore::getInstance().getBookStats(cachePath);
}

void BookReadingStats::formatDuration(uint32_t seconds, char* buf, size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (hours == 0) {
    snprintf(buf, len, "%lu min", static_cast<unsigned long>(minutes));
  } else {
    snprintf(buf, len, "%luh %lu min", static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  }
}

void BookReadingStats::save(const std::string& cachePath) const {
  ReadingStatsStore::getInstance().updateBookStats(cachePath, *this);
}

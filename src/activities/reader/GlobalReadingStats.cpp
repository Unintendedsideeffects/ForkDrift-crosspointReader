#include "GlobalReadingStats.h"

#include "ReadingStatsStore.h"

GlobalReadingStats GlobalReadingStats::load() {
  ReadingStatsStore::getInstance().ensureLoaded();
  return ReadingStatsStore::getInstance().getGlobalStats();
}

void GlobalReadingStats::save() const { ReadingStatsStore::getInstance().updateGlobalStats(*this); }

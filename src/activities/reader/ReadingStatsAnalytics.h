#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"

namespace ReadingStatsAnalytics {

struct DailySessionEstimate {
  uint32_t dayOrdinal = 0;
  uint32_t sessions = 0;
};

struct SummaryMetrics {
  uint32_t totalPages = 0;
  uint32_t totalSeconds = 0;
  double averageMinutesPerReadingDay = 0.0;
};

struct StreakMetrics {
  uint32_t currentStreak = 0;
  uint32_t longestStreak = 0;
};

struct GoalProgress {
  uint64_t readingMs = 0;
  uint64_t goalMs = 0;
  double percent = 0.0;
  bool met = false;
};

std::vector<DailySessionEstimate> estimateSessionsPerDay(const ReadingStatsStore& store);
SummaryMetrics summarize(const ReadingStatsStore& store);
StreakMetrics calculateStreaks(const ReadingStatsStore& store, uint32_t referenceDayOrdinal, uint64_t dailyGoalMs);
GoalProgress calculateGoalProgress(const ReadingStatsStore& store, uint32_t dayOrdinal, uint32_t dailyGoalMinutes);

uint32_t dayOrdinalFromDate(int year, unsigned month, unsigned day);
void dateFromDayOrdinal(uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day);
std::string formatMonthLabel(int year, unsigned month);
std::string formatDayLabel(uint32_t dayOrdinal);
std::string formatDuration(uint64_t readingMs);

}  // namespace ReadingStatsAnalytics

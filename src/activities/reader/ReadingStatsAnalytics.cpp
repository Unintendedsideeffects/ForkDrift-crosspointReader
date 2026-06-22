#include "ReadingStatsAnalytics.h"

#include <algorithm>
#include <cstdio>

namespace {

int32_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

void civilFromDays(int z, int& year, unsigned& month, unsigned& day) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned dayOfEra = static_cast<unsigned>(z - era * 146097);
  const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  year = static_cast<int>(yearOfEra) + era * 400;
  const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const unsigned monthPart = (5 * dayOfYear + 2) / 153;
  day = dayOfYear - (153 * monthPart + 2) / 5 + 1;
  month = monthPart + (monthPart < 10 ? 3 : -9);
  year += month <= 2;
}

uint32_t estimateSessionsForDay(const ReadingBookStats& book, const ReadingDayStats& day) {
  if (book.sessions == 0 || day.readingMs == 0) {
    return 0;
  }
  if (book.totalReadingMs == 0) {
    return 1;
  }
  uint32_t estimate =
      static_cast<uint32_t>((book.sessions * day.readingMs + book.totalReadingMs / 2) / book.totalReadingMs);
  return estimate == 0 ? 1 : std::min(estimate, book.sessions);
}

}  // namespace

namespace ReadingStatsAnalytics {

std::vector<DailySessionEstimate> estimateSessionsPerDay(const ReadingStatsStore& store) {
  std::vector<DailySessionEstimate> result;
  for (const auto& book : store.getBooks()) {
    for (const auto& day : book.readingDays) {
      if (day.dayOrdinal == 0) {
        continue;
      }
      const uint32_t sessions = estimateSessionsForDay(book, day);
      if (sessions == 0) {
        continue;
      }
      auto it = std::lower_bound(
          result.begin(), result.end(), day.dayOrdinal,
          [](const DailySessionEstimate& entry, uint32_t ordinal) { return entry.dayOrdinal < ordinal; });
      if (it == result.end() || it->dayOrdinal != day.dayOrdinal) {
        result.insert(it, DailySessionEstimate{day.dayOrdinal, sessions});
      } else {
        it->sessions += sessions;
      }
    }
  }
  return result;
}

SummaryMetrics summarize(const ReadingStatsStore& store) {
  SummaryMetrics metrics;
  const GlobalReadingStats global = store.getGlobalStats();
  metrics.totalPages = global.totalPagesTurned;
  metrics.totalSeconds = global.totalReadingSeconds;

  const auto& days = store.getReadingDays();
  const uint32_t readingDays = static_cast<uint32_t>(
      std::count_if(days.begin(), days.end(), [](const ReadingDayStats& day) { return day.readingMs > 0; }));
  if (readingDays > 0) {
    metrics.averageMinutesPerReadingDay = static_cast<double>(metrics.totalSeconds) / 60.0 / readingDays;
  }
  return metrics;
}

StreakMetrics calculateStreaks(const ReadingStatsStore& store, uint32_t referenceDayOrdinal, uint64_t dailyGoalMs) {
  std::vector<uint32_t> eligibleDays;
  for (const auto& day : store.getReadingDays()) {
    if (day.dayOrdinal <= referenceDayOrdinal && day.readingMs >= dailyGoalMs) {
      eligibleDays.push_back(day.dayOrdinal);
    }
  }
  std::sort(eligibleDays.begin(), eligibleDays.end());
  eligibleDays.erase(std::unique(eligibleDays.begin(), eligibleDays.end()), eligibleDays.end());

  StreakMetrics metrics;
  uint32_t run = 0;
  uint32_t previous = 0;
  for (uint32_t day : eligibleDays) {
    run = (run > 0 && day == previous + 1) ? run + 1 : 1;
    previous = day;
    metrics.longestStreak = std::max(metrics.longestStreak, run);
  }

  if (!eligibleDays.empty()) {
    const uint32_t latest = eligibleDays.back();
    if (latest == referenceDayOrdinal || latest + 1 == referenceDayOrdinal) {
      metrics.currentStreak = 1;
      for (size_t i = eligibleDays.size() - 1; i > 0; --i) {
        if (eligibleDays[i] != eligibleDays[i - 1] + 1) {
          break;
        }
        metrics.currentStreak++;
      }
    }
  }
  return metrics;
}

GoalProgress calculateGoalProgress(const ReadingStatsStore& store, uint32_t dayOrdinal, uint32_t dailyGoalMinutes) {
  GoalProgress progress;
  progress.goalMs = static_cast<uint64_t>(dailyGoalMinutes) * 60ULL * 1000ULL;
  const auto& days = store.getReadingDays();
  auto it = std::find_if(days.begin(), days.end(),
                         [dayOrdinal](const ReadingDayStats& day) { return day.dayOrdinal == dayOrdinal; });
  if (it != days.end()) {
    progress.readingMs = it->readingMs;
  }
  if (progress.goalMs > 0) {
    progress.percent = static_cast<double>(progress.readingMs) * 100.0 / progress.goalMs;
    progress.met = progress.readingMs >= progress.goalMs;
  }
  return progress;
}

uint32_t dayOrdinalFromDate(int year, unsigned month, unsigned day) {
  return static_cast<uint32_t>(daysFromCivil(year, month, day));
}

void dateFromDayOrdinal(uint32_t dayOrdinal, int& year, unsigned& month, unsigned& day) {
  civilFromDays(static_cast<int>(dayOrdinal), year, month, day);
}

std::string formatMonthLabel(int year, unsigned month) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u", year, month);
  return buffer;
}

std::string formatDayLabel(uint32_t dayOrdinal) {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  dateFromDayOrdinal(dayOrdinal, year, month, day);
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
  return buffer;
}

std::string formatDuration(uint64_t readingMs) {
  const uint64_t minutesTotal = readingMs / 60000ULL;
  const uint64_t hours = minutesTotal / 60ULL;
  const uint64_t minutes = minutesTotal % 60ULL;
  if (hours == 0) {
    return std::to_string(minutes) + "m";
  }
  return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

}  // namespace ReadingStatsAnalytics

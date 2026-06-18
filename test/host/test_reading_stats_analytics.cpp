#include "JsonSettingsIO.h"
#include "doctest/doctest.h"
#include "src/activities/reader/ReadingStatsAnalytics.h"

TEST_CASE("reading stats analytics summarizes imported stats") {
  ReadingStatsStore store;
  REQUIRE(JsonSettingsIO::loadReadingStats(
      store,
      R"({"globalPagesTurned":120,"books":[{"cachePath":"book-a","totalReadingMs":60000,"sessions":2,"totalPagesTurned":20},{"cachePath":"book-b","totalReadingMs":120000,"sessions":1,"totalPagesTurned":10}],"readingDays":[{"dayOrdinal":100,"readingMs":60000},{"dayOrdinal":101,"readingMs":120000}]})"));

  const auto summary = ReadingStatsAnalytics::summarize(store);
  CHECK(summary.totalPages == 120);
  CHECK(summary.totalSeconds == 180);
  CHECK(summary.averageMinutesPerReadingDay == doctest::Approx(1.5));
}

TEST_CASE("reading stats analytics calculates streaks and goal progress") {
  ReadingStatsStore store;
  REQUIRE(JsonSettingsIO::loadReadingStats(
      store,
      R"({"readingDays":[{"dayOrdinal":100,"readingMs":700000},{"dayOrdinal":101,"readingMs":300000},{"dayOrdinal":102,"readingMs":600000},{"dayOrdinal":103,"readingMs":900000}]})"));

  const auto streaks = ReadingStatsAnalytics::calculateStreaks(store, 103, 600000);
  CHECK(streaks.longestStreak == 2);
  CHECK(streaks.currentStreak == 2);
  CHECK(ReadingStatsAnalytics::calculateStreaks(store, 104, 600000).currentStreak == 2);
  CHECK(ReadingStatsAnalytics::calculateStreaks(store, 105, 600000).currentStreak == 0);

  const auto progress = ReadingStatsAnalytics::calculateGoalProgress(store, 101, 10);
  CHECK(progress.readingMs == 300000);
  CHECK(progress.goalMs == 600000);
  CHECK(progress.percent == doctest::Approx(50.0));
  CHECK_FALSE(progress.met);
}

TEST_CASE("reading stats analytics estimates sessions per day and formats labels") {
  ReadingStatsStore store;
  REQUIRE(JsonSettingsIO::loadReadingStats(
      store,
      R"({"books":[{"cachePath":"book-a","totalReadingMs":10000,"sessions":10,"readingDays":[{"dayOrdinal":100,"readingMs":3000},{"dayOrdinal":101,"readingMs":7000}]},{"cachePath":"book-b","totalReadingMs":5000,"sessions":5,"readingDays":[{"dayOrdinal":101,"readingMs":5000}]}]})"));

  const auto sessions = ReadingStatsAnalytics::estimateSessionsPerDay(store);
  REQUIRE(sessions.size() == 2);
  CHECK(sessions[0].dayOrdinal == 100);
  CHECK(sessions[0].sessions == 3);
  CHECK(sessions[1].dayOrdinal == 101);
  CHECK(sessions[1].sessions == 12);

  CHECK(ReadingStatsAnalytics::dayOrdinalFromDate(1970, 1, 1) == 0);
  CHECK(ReadingStatsAnalytics::formatDayLabel(0) == "1970-01-01");
  CHECK(ReadingStatsAnalytics::formatMonthLabel(2026, 6) == "2026-06");
  CHECK(ReadingStatsAnalytics::formatDuration(65ULL * 60000ULL) == "1h 5m");
}

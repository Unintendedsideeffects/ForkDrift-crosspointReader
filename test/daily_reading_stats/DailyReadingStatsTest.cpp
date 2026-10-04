#include <CrossPointSettings.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "DailyReadingStats.h"
#include "StatsUploadPayload.h"
using namespace DailyReadingStats;
static ReadingStatsDateTime dt(uint8_t day, uint8_t hour = 0, uint8_t minute = 0, uint8_t second = 0) {
  return {{2026, 10, day}, hour, minute, second};
}
static uint32_t dayOf(uint8_t day) { return readingStatsDayIndex(dt(day).date); }
static uint32_t secondsOn(uint8_t day) {
  Counter c;
  EXPECT_TRUE(flush());
  EXPECT_TRUE(read(dayOf(day), c));
  return c.seconds;
}
TEST(DailyReading, SplitsMidnightAndKeepsSeconds) {
  Session s;
  s.start(dt(1, 23, 59, 45), 48);
  EXPECT_TRUE(s.accept(dt(2, 0, 0, 15), 48, 30, 30));
  EXPECT_EQ(secondsOn(1), 15u);
  EXPECT_EQ(secondsOn(2), 15u);
  s.start(dt(2, 0, 1, 0), 48);
  EXPECT_TRUE(s.accept(dt(2, 0, 1, 11), 48, 11, 41));
  EXPECT_EQ(secondsOn(2), 26u);
}
TEST(DailyReading, PreservesSessionMinimumAndPauses) {
  Session s;
  s.start(dt(3, 12), 48);
  EXPECT_TRUE(s.accept(dt(3, 12, 0, 5), 48, 5, 5));
  EXPECT_EQ(secondsOn(3), 0u);
  // An overlay/idle boundary resumes at 13:00, never at the session's 12:00 start.
  s.start(dt(3, 13), 48);
  EXPECT_TRUE(s.accept(dt(3, 13, 0, 5), 48, 5, 10));
  EXPECT_EQ(secondsOn(3), 10u);
  Session shortSession;
  shortSession.start(dt(4, 12), 48);
  shortSession.accept(dt(4, 12, 0, 9), 48, 9, 9);
  shortSession.reset();
  EXPECT_EQ(secondsOn(4), 0u);
}
TEST(DailyReading, RejectsClockChangesAndUnavailableDates) {
  Session s;
  s.start({}, 48);
  EXPECT_TRUE(s.accept(dt(5, 12), 48, 60, 60));
  s.start(dt(5, 12), 48);
  EXPECT_TRUE(s.accept(dt(5, 13), 48, 60, 120));
  s.start(dt(5, 12), 48);
  EXPECT_TRUE(s.accept(dt(5, 12, 1), 49, 60, 180));
  s.start(dt(5, 12), 48);
  EXPECT_TRUE(s.accept({}, 48, 60, 240));
  EXPECT_EQ(secondsOn(5), 0u);
}
TEST(DailyReading, PendingShortIntervalsCanSpanNineDays) {
  Session s;
  for (uint8_t day = 6; day < 15; ++day) {
    s.start(dt(day, 12), 48);
    EXPECT_TRUE(s.accept(dt(day, 12, 0, 1), 48, 1, day - 5));
  }
  s.start(dt(15, 23, 59, 59), 48);
  EXPECT_TRUE(s.accept(dt(16, 0, 0, 1), 48, 2, 11));
  EXPECT_EQ(secondsOn(6), 1u);
  EXPECT_EQ(secondsOn(15), 1u);
  EXPECT_EQ(secondsOn(16), 1u);
}
TEST(DailyReading, DebouncesAndRetainsMonotonicAcknowledgements) {
  const uint32_t day = dayOf(17);
  EXPECT_TRUE(record(day, 11));
  Counter c;
  EXPECT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 0u);  // no page-turn write yet
  EXPECT_TRUE(flush());
  EXPECT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 11u);
  c.uploaded = c.seconds;
  EXPECT_TRUE(write(day, c));
  EXPECT_TRUE(record(day, 9));
  EXPECT_TRUE(flush());
  EXPECT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 20u);
  EXPECT_EQ(c.uploaded, 11u);
  EXPECT_FALSE(write(day, {19, 11}));
}
TEST(DailyReading, SecondsForIncludesUncheckpointedSeconds) {
  const uint32_t day = dayOf(28);
  uint32_t seconds = 99;
  EXPECT_TRUE(secondsFor(dayOf(29), seconds));
  EXPECT_EQ(seconds, 0u);  // missing day reads as zero
  EXPECT_TRUE(record(day, 7));
  EXPECT_TRUE(secondsFor(day, seconds));
  EXPECT_EQ(seconds, 7u);  // cached before the 60-second checkpoint
  EXPECT_TRUE(flush());
  EXPECT_TRUE(secondsFor(day, seconds));
  EXPECT_EQ(seconds, 7u);
}
TEST(DailyReading, SummarizesTodayAndSevenCalendarDays) {
  const auto day = [](uint8_t month, uint8_t d) { return readingStatsDayIndex({2026, month, d}); };
  EXPECT_TRUE(write(day(10, 31), {600, 0}));  // outside the window
  EXPECT_TRUE(write(day(11, 1), {700, 0}));
  EXPECT_TRUE(write(day(11, 4), {70, 0}));
  EXPECT_TRUE(record(day(11, 7), 30));  // cached, not yet checkpointed
  Summary summary = summarize(day(11, 7));
  EXPECT_TRUE(summary.hasToday);
  EXPECT_EQ(summary.todaySeconds, 30u);
  EXPECT_TRUE(summary.hasSevenDayAverage);
  EXPECT_EQ(summary.sevenDayAverageSeconds, 114u);  // (700 + 70 + 30) / 7, zero days included
  summary = summarize(day(11, 9));
  EXPECT_TRUE(summary.hasToday);
  EXPECT_EQ(summary.todaySeconds, 0u);
  EXPECT_EQ(summary.sevenDayAverageSeconds, 14u);  // (70 + 30) / 7

  char path[64];
  snprintf(path, sizeof(path), "%s/%05u.bin", DIRECTORY, day(11, 12));
  FsFile f;
  ASSERT_TRUE(Storage.openFileForWrite("test", path, f));
  uint8_t corrupt[13] = {1};
  f.write(corrupt, sizeof(corrupt));
  f.close();
  summary = summarize(day(11, 14));
  EXPECT_TRUE(summary.hasToday);
  EXPECT_FALSE(summary.hasSevenDayAverage);
  summary = summarize(day(11, 12));
  EXPECT_FALSE(summary.hasToday);
  EXPECT_FALSE(summary.hasSevenDayAverage);
}
TEST(DailyReading, RecoversInterruptedCheckpointAndRejectsFutureOrCorruptFormats) {
  const uint32_t day = dayOf(18);
  EXPECT_TRUE(write(day, {23, 0}));
  char path[64];
  snprintf(path, sizeof(path), "%s/%05u.bin.tmp", DIRECTORY, day);
  Storage.failNextRenameFrom(path);
  EXPECT_FALSE(write(day, {41, 0}));
  Counter c;
  EXPECT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 41u);  // newest verified temp wins
  EXPECT_TRUE(write(day, c));
  EXPECT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 41u);
  snprintf(path, sizeof(path), "%s/%05u.bin", DIRECTORY, dayOf(19));
  FsFile f;
  ASSERT_TRUE(Storage.openFileForWrite("test", path, f));
  uint8_t future[13] = {2};
  f.write(future, sizeof(future));
  f.close();
  EXPECT_FALSE(read(dayOf(19), c));
  EXPECT_FALSE(write(dayOf(19), {1, 0}));
  snprintf(path, sizeof(path), "%s/%05u.bin", DIRECTORY, dayOf(20));
  ASSERT_TRUE(Storage.openFileForWrite("test", path, f));
  uint8_t corrupt[13] = {1};
  f.write(corrupt, sizeof(corrupt));
  f.close();
  EXPECT_FALSE(read(dayOf(20), c));
  EXPECT_FALSE(write(dayOf(20), {1, 0}));
}
TEST(DailyReading, LocalOffsetAndDateValidation) {
  ReadingStatsDateTime out;
  SETTINGS.clockUtcOffsetQ = 47;
  EXPECT_TRUE(getCurrentLocalDailyReadingDateTime(out));
  EXPECT_EQ(out.date.day, 30);
  EXPECT_EQ(out.date.month, 9);
  EXPECT_EQ(out.hour, 23);
  EXPECT_EQ(out.minute, 45);
  EXPECT_EQ(out.second, 10);
  SETTINGS.clockUtcOffsetQ = 255;
  EXPECT_FALSE(getCurrentLocalDailyReadingDateTime(out));
  SETTINGS.clockUtcOffsetQ = 48;
  halClock.valid = false;
  EXPECT_FALSE(getCurrentLocalDailyReadingDateTime(out));
  halClock.valid = true;
  EXPECT_FALSE((ReadingStatsDateTime{{2026, 2, 29}, 0, 0, 0}.isValid()));
  EXPECT_FALSE((ReadingStatsDateTime{{2026, 1, 1}, 24, 0, 0}.isValid()));
  EXPECT_TRUE((ReadingStatsDateTime{{2028, 2, 29}, 23, 59, 59}.isValid()));
}
TEST(DailyReading, BoundedPayloadWithOptionalDailySeconds) {
  char payload[StatsUploadPayload::CAPACITY];
  GlobalReadingStats g;
  g.totalReadingSeconds = 12345;
  ASSERT_TRUE(StatsUploadPayload::global(payload, sizeof(payload), "crossink-test", g, dayOf(21), 61));
  EXPECT_NE(std::string(payload).find("\"daily\":[{\"date\":\"2026-10-21\",\"seconds\":61}]"), std::string::npos);
  ASSERT_TRUE(StatsUploadPayload::global(payload, sizeof(payload), "crossink-test", g));
  EXPECT_EQ(std::string(payload).find("daily"), std::string::npos);
  EXPECT_FALSE(StatsUploadPayload::global(payload, 20, "crossink-test", g, dayOf(21), 61));
}

TEST(DailyReading, KeepsDeferredShortSecondsAfterTransientStorageFault) {
  Session s;
  ASSERT_TRUE(record(dayOf(22), 11));
  ASSERT_TRUE(record(dayOf(23), 11));
  s.start(dt(24, 12), 48);
  ASSERT_TRUE(s.accept(dt(24, 12, 0, 5), 48, 5, 5));
  char temp[64];
  snprintf(temp, sizeof(temp), "%s/%05u.bin.tmp", DIRECTORY, dayOf(23));
  Storage.failNextRenameFrom(temp);  // Failure evicting the midnight cache slot before accepting the pending day.
  s.start(dt(24, 13), 48);
  EXPECT_FALSE(s.accept(dt(24, 13, 0, 5), 48, 5, 10));
  s.start(dt(24, 14), 48);
  EXPECT_TRUE(s.accept(dt(24, 14, 0, 5), 48, 5, 15));
  EXPECT_EQ(secondsOn(24), 15u);
}
TEST(DailyReading, RecoveredUploadAckCanRetryAfterPublishFailure) {
  const uint32_t day = dayOf(25);
  ASSERT_TRUE(record(day, 60));
  Counter c;
  ASSERT_TRUE(read(day, c));
  c.uploaded = c.seconds;
  char temp[64];
  snprintf(temp, sizeof(temp), "%s/%05u.bin.tmp", DIRECTORY, day);
  Storage.failNextRenameFrom(temp);
  EXPECT_FALSE(write(day, c));
  ASSERT_TRUE(record(day, 10));
  ASSERT_TRUE(flush());
  ASSERT_TRUE(read(day, c));
  EXPECT_EQ(c.seconds, 70u);
  EXPECT_EQ(c.uploaded, 60u);
}

TEST(DailyReading, ReaderGateRejectsIdleAndStoppedSleepOrOverlayTimers) {
  uint32_t seconds = 0;
  EXPECT_TRUE(readingStatsIntervalSeconds(121000, 1000, 120, seconds));
  EXPECT_EQ(seconds, 120u);
  EXPECT_TRUE(readingStatsIntervalSeconds(121999, 1000, 120, seconds));
  EXPECT_EQ(seconds, 120u);
  EXPECT_FALSE(readingStatsIntervalSeconds(122000, 1000, 120, seconds));
  EXPECT_EQ(seconds, 0u);
  EXPECT_FALSE(readingStatsIntervalSeconds(100000, 0, 120, seconds));  // sleep/lock/overlay stop
  EXPECT_FALSE(readingStatsIntervalSeconds(1999, 1000, 120, seconds));
  EXPECT_TRUE(readingStatsIntervalSeconds(3999, UINT32_MAX - 1000, 120, seconds));
  EXPECT_EQ(seconds, 5u);
  EXPECT_TRUE(readingStatsIntervalSeconds(200500, 200000, 120, seconds) ==
              false);  // fresh resume starts a new interval
}

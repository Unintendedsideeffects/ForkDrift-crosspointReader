#include "doctest/doctest.h"
#include "src/activities/reader/BookReadingStats.h"
#include "src/activities/reader/GlobalReadingStats.h"
#include "src/activities/reader/ReadingStatsStore.h"
#include "test/mock/Arduino.h"
#include "test/mock/HalStorage.h"

TEST_CASE("reading stats store records sessions and page turns") {
  Storage.reset();
  mockMillisVal = 0;
  ReadingStatsStore::getInstance().reset();

  ReadingStatsStore::getInstance().beginSession("/cache/book", "/books/book.epub", "Book", "Author",
                                                "/covers/book.bmp");
  mockMillisVal += 120000;
  ReadingStatsStore::getInstance().recordPageTurn("/cache/book");
  ReadingStatsStore::getInstance().endSession();

  const BookReadingStats bookStats = BookReadingStats::load("/cache/book");
  CHECK(bookStats.sessionCount == 1);
  CHECK(bookStats.totalReadingSeconds == 120);
  CHECK(bookStats.totalPagesTurned == 1);

  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  CHECK(globalStats.totalSessions == 1);
  CHECK(globalStats.totalReadingSeconds == 120);
  CHECK(globalStats.totalPagesTurned == 1);
}

TEST_CASE("reading stats store round trips canonical json") {
  Storage.reset();
  mockMillisVal = 0;
  ReadingStatsStore::getInstance().reset();

  ReadingStatsStore::getInstance().beginSession("/cache/roundtrip", "/books/roundtrip.epub", "Roundtrip", "Author", "");
  mockMillisVal += 90000;
  ReadingStatsStore::getInstance().endSession();
  REQUIRE(ReadingStatsStore::getInstance().saveToFile());

  REQUIRE(ReadingStatsStore::getInstance().loadFromFile());

  const BookReadingStats stats = BookReadingStats::load("/cache/roundtrip");
  CHECK(stats.sessionCount == 1);
  CHECK(stats.totalReadingSeconds == 90);
}

TEST_CASE("book and global adapters update canonical store") {
  Storage.reset();
  mockMillisVal = 0;
  ReadingStatsStore::getInstance().reset();

  BookReadingStats bookStats;
  bookStats.sessionCount = 3;
  bookStats.totalReadingSeconds = 600;
  bookStats.totalPagesTurned = 42;
  bookStats.isCompleted = true;
  bookStats.save("/cache/adapter");

  const BookReadingStats loadedBookStats = BookReadingStats::load("/cache/adapter");
  CHECK(loadedBookStats.sessionCount == 3);
  CHECK(loadedBookStats.totalReadingSeconds == 600);
  CHECK(loadedBookStats.totalPagesTurned == 42);
  CHECK(loadedBookStats.isCompleted);

  GlobalReadingStats globalStats = GlobalReadingStats::load();
  globalStats.totalPagesTurned = 77;
  globalStats.save();
  CHECK(GlobalReadingStats::load().totalPagesTurned == 77);
}

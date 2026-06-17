// Source fork: https://github.com/franssjz/cpr-vcodex
// Adapted for ForkDrift as the canonical no-legacy reading stats store.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"

class HalFile;

struct ReadingDayStats {
  uint32_t dayOrdinal = 0;
  uint64_t readingMs = 0;
};

struct ReadingBookStats {
  std::string cachePath;
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
  std::vector<ReadingDayStats> readingDays;
  uint64_t totalReadingMs = 0;
  uint32_t sessions = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t lastSessionMs = 0;
  uint8_t lastProgressPercent = 0;
  bool completed = false;
};

struct ReadingSessionSnapshot {
  bool valid = false;
  std::string cachePath;
  std::string path;
  uint32_t sessionMs = 0;
  bool counted = false;
};

class ReadingStatsStore;
namespace JsonSettingsIO {
bool saveReadingStats(const ReadingStatsStore& store, const char* path);
bool loadReadingStats(ReadingStatsStore& store, const char* json);
bool loadReadingStats(ReadingStatsStore& store, HalFile& file);
}  // namespace JsonSettingsIO

class ReadingStatsStore {
  static ReadingStatsStore instance;

  std::vector<ReadingBookStats> books;
  std::vector<ReadingDayStats> readingDays;
  ReadingSessionSnapshot lastSessionSnapshot;
  bool activeSession = false;
  size_t activeBookIndex = 0;
  unsigned long sessionStartMs = 0;
  unsigned long lastActivityMs = 0;
  uint64_t activeAccumulatedMs = 0;
  uint32_t globalPagesTurned = 0;
  bool loaded = false;
  mutable bool dirty = false;
  mutable unsigned long lastSaveMs = 0;

  friend bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, const char*);
  friend bool JsonSettingsIO::loadReadingStats(ReadingStatsStore&, HalFile&);

  size_t findBookIndexByCachePath(const std::string& cachePath) const;
  ReadingBookStats& getOrCreateBook(const std::string& cachePath, const std::string& path, const std::string& title,
                                    const std::string& author, const std::string& coverBmpPath);
  void addReadingTime(ReadingBookStats& book, uint64_t readingMs);
  void addReadingDay(uint64_t readingMs);
  bool shouldSaveDeferred() const;

 public:
  static ReadingStatsStore& getInstance() { return instance; }

  void beginSession(const std::string& cachePath, const std::string& path, const std::string& title,
                    const std::string& author, const std::string& coverBmpPath);
  void noteActivity();
  void updateProgress(const std::string& cachePath, uint8_t progressPercent, bool completed);
  void endSession();
  void recordPageTurn(const std::string& cachePath, uint32_t count = 1);
  void updateBookStats(const std::string& cachePath, const BookReadingStats& stats);
  void updateGlobalStats(const GlobalReadingStats& stats);

  BookReadingStats getBookStats(const std::string& cachePath) const;
  GlobalReadingStats getGlobalStats() const;
  const ReadingBookStats* findBook(const std::string& cachePath) const;
  const std::vector<ReadingBookStats>& getBooks() const { return books; }
  const std::vector<ReadingDayStats>& getReadingDays() const { return readingDays; }
  const ReadingSessionSnapshot& getLastSessionSnapshot() const { return lastSessionSnapshot; }

  void markDirty() const;
  void ensureLoaded();
  void reset();
  bool saveToFile() const;
  bool loadFromFile();
};

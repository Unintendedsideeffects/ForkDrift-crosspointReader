// Source fork: https://github.com/franssjz/cpr-vcodex
// Adapted for ForkDrift as the canonical no-legacy reading stats store.
#include "ReadingStatsStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <algorithm>
#include <ctime>

namespace {
constexpr char READING_STATS_FILE_JSON[] = "/.crosspoint/reading_stats.json";
constexpr unsigned long MAX_READING_GAP_MS = 30UL * 60UL * 1000UL;
constexpr unsigned long DEFERRED_SAVE_INTERVAL_MS = 30UL * 1000UL;
constexpr unsigned long MIN_COUNTED_SESSION_MS = 60UL * 1000UL;

uint8_t clampPercent(uint8_t percent) { return percent > 100 ? 100 : percent; }

uint32_t currentDayOrdinal() {
  const std::time_t now = std::time(nullptr);
  if (now <= 0) {
    return 0;
  }
  return static_cast<uint32_t>(now / 86400);
}

void addReadingToDays(std::vector<ReadingDayStats>& days, uint32_t dayOrdinal, uint64_t readingMs) {
  if (dayOrdinal == 0 || readingMs == 0) {
    return;
  }
  auto it = std::lower_bound(days.begin(), days.end(), dayOrdinal,
                             [](const ReadingDayStats& day, uint32_t ordinal) { return day.dayOrdinal < ordinal; });
  if (it == days.end() || it->dayOrdinal != dayOrdinal) {
    days.insert(it, ReadingDayStats{dayOrdinal, readingMs});
  } else {
    it->readingMs += readingMs;
  }
}
}  // namespace

ReadingStatsStore ReadingStatsStore::instance;

size_t ReadingStatsStore::findBookIndexByCachePath(const std::string& cachePath) const {
  if (cachePath.empty()) {
    return books.size();
  }
  for (size_t i = 0; i < books.size(); ++i) {
    if (books[i].cachePath == cachePath) {
      return i;
    }
  }
  return books.size();
}

ReadingBookStats& ReadingStatsStore::getOrCreateBook(const std::string& cachePath, const std::string& path,
                                                     const std::string& title, const std::string& author,
                                                     const std::string& coverBmpPath) {
  size_t index = findBookIndexByCachePath(cachePath);
  if (index == books.size()) {
    ReadingBookStats book;
    book.cachePath = cachePath;
    book.path = path;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    books.push_back(std::move(book));
    markDirty();
    return books.back();
  }

  auto& book = books[index];
  if (!path.empty() && book.path != path) {
    book.path = path;
    markDirty();
  }
  if (!title.empty() && book.title != title) {
    book.title = title;
    markDirty();
  }
  if (!author.empty() && book.author != author) {
    book.author = author;
    markDirty();
  }
  if (!coverBmpPath.empty() && book.coverBmpPath != coverBmpPath) {
    book.coverBmpPath = coverBmpPath;
    markDirty();
  }
  return book;
}

void ReadingStatsStore::addReadingDay(uint64_t readingMs) {
  addReadingToDays(readingDays, currentDayOrdinal(), readingMs);
}

void ReadingStatsStore::addReadingTime(ReadingBookStats& book, uint64_t readingMs) {
  if (readingMs == 0) {
    return;
  }
  book.totalReadingMs += readingMs;
  addReadingToDays(book.readingDays, currentDayOrdinal(), readingMs);
  addReadingDay(readingMs);
  markDirty();
}

bool ReadingStatsStore::shouldSaveDeferred() const {
  if (!dirty) {
    return false;
  }
  if (!activeSession) {
    return true;
  }
  return lastSaveMs == 0 || (millis() - lastSaveMs) >= DEFERRED_SAVE_INTERVAL_MS;
}

void ReadingStatsStore::beginSession(const std::string& cachePath, const std::string& path, const std::string& title,
                                     const std::string& author, const std::string& coverBmpPath) {
  if (cachePath.empty()) {
    return;
  }
  ensureLoaded();
  if (activeSession) {
    endSession();
  }
  getOrCreateBook(cachePath, path, title, author, coverBmpPath);
  activeBookIndex = findBookIndexByCachePath(cachePath);
  activeSession = activeBookIndex < books.size();
  activeAccumulatedMs = 0;
  sessionStartMs = millis();
  lastActivityMs = sessionStartMs;
}

void ReadingStatsStore::noteActivity() {
  if (!activeSession || activeBookIndex >= books.size()) {
    return;
  }
  const unsigned long nowMs = millis();
  const unsigned long elapsedMs = nowMs - lastActivityMs;
  const unsigned long creditedMs = std::min(elapsedMs, MAX_READING_GAP_MS);
  if (creditedMs > 0) {
    addReadingTime(books[activeBookIndex], creditedMs);
    activeAccumulatedMs += creditedMs;
  }
  lastActivityMs = nowMs;
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

void ReadingStatsStore::updateProgress(const std::string& cachePath, uint8_t progressPercent, bool completed) {
  ensureLoaded();
  const size_t index = findBookIndexByCachePath(cachePath);
  if (index >= books.size()) {
    return;
  }
  auto& book = books[index];
  const uint8_t clamped = clampPercent(progressPercent);
  const bool nextCompleted = completed || clamped >= 100;
  if (book.lastProgressPercent == clamped && book.completed == nextCompleted) {
    return;
  }
  book.lastProgressPercent = clamped;
  book.completed = nextCompleted;
  markDirty();
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

void ReadingStatsStore::endSession() {
  if (!activeSession || activeBookIndex >= books.size()) {
    activeSession = false;
    lastSessionSnapshot = {};
    return;
  }
  noteActivity();
  auto& book = books[activeBookIndex];
  const bool counted = activeAccumulatedMs >= MIN_COUNTED_SESSION_MS;
  const uint32_t sessionMs = activeAccumulatedMs > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(activeAccumulatedMs);
  if (counted) {
    book.sessions++;
    book.lastSessionMs = sessionMs;
    markDirty();
  }
  lastSessionSnapshot = ReadingSessionSnapshot{true, book.cachePath, book.path, sessionMs, counted};
  activeSession = false;
  activeAccumulatedMs = 0;
  saveToFile();
}

void ReadingStatsStore::recordPageTurn(const std::string& cachePath, uint32_t count) {
  if (count == 0) {
    return;
  }
  ensureLoaded();
  auto& book = getOrCreateBook(cachePath, "", "", "", "");
  book.totalPagesTurned += count;
  globalPagesTurned += count;
  markDirty();
  if (shouldSaveDeferred()) {
    saveToFile();
  }
}

void ReadingStatsStore::updateBookStats(const std::string& cachePath, const BookReadingStats& stats) {
  ensureLoaded();
  auto& book = getOrCreateBook(cachePath, "", "", "", "");
  book.sessions = stats.sessionCount;
  book.totalReadingMs = static_cast<uint64_t>(stats.totalReadingSeconds) * 1000ULL;
  book.totalPagesTurned = stats.totalPagesTurned;
  book.completed = stats.isCompleted;
  markDirty();
  saveToFile();
}

void ReadingStatsStore::updateGlobalStats(const GlobalReadingStats& stats) {
  ensureLoaded();
  globalPagesTurned = stats.totalPagesTurned;
  markDirty();
  saveToFile();
}

BookReadingStats ReadingStatsStore::getBookStats(const std::string& cachePath) const {
  BookReadingStats stats;
  const ReadingBookStats* book = findBook(cachePath);
  if (!book) {
    return stats;
  }
  stats.sessionCount = book->sessions > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(book->sessions);
  const uint64_t totalSeconds = book->totalReadingMs / 1000ULL;
  stats.totalReadingSeconds = totalSeconds > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(totalSeconds);
  stats.totalPagesTurned = book->totalPagesTurned;
  stats.isCompleted = book->completed;
  return stats;
}

GlobalReadingStats ReadingStatsStore::getGlobalStats() const {
  GlobalReadingStats stats;
  stats.totalPagesTurned = globalPagesTurned;
  for (const auto& book : books) {
    stats.totalSessions += book.sessions;
    stats.totalReadingSeconds += static_cast<uint32_t>(
        std::min<uint64_t>(book.totalReadingMs / 1000ULL, UINT32_MAX - stats.totalReadingSeconds));
    if (book.completed) {
      stats.completedBooks++;
    }
  }
  return stats;
}

const ReadingBookStats* ReadingStatsStore::findBook(const std::string& cachePath) const {
  const size_t index = findBookIndexByCachePath(cachePath);
  return index < books.size() ? &books[index] : nullptr;
}

void ReadingStatsStore::markDirty() const { dirty = true; }

void ReadingStatsStore::ensureLoaded() {
  if (!loaded) {
    loadFromFile();
  }
}

void ReadingStatsStore::reset() {
  books.clear();
  readingDays.clear();
  lastSessionSnapshot = {};
  activeSession = false;
  activeBookIndex = 0;
  activeAccumulatedMs = 0;
  globalPagesTurned = 0;
  loaded = true;
  markDirty();
  saveToFile();
}

bool ReadingStatsStore::saveToFile() const {
  if (!dirty && Storage.exists(READING_STATS_FILE_JSON)) {
    return true;
  }
  if (activeSession && !shouldSaveDeferred()) {
    return true;
  }
  Storage.mkdir("/.crosspoint");
  const bool saved = JsonSettingsIO::saveReadingStats(*this, READING_STATS_FILE_JSON);
  if (saved) {
    dirty = false;
    lastSaveMs = millis();
  }
  return saved;
}

bool ReadingStatsStore::loadFromFile() {
  if (!Storage.exists(READING_STATS_FILE_JSON)) {
    books.clear();
    readingDays.clear();
    lastSessionSnapshot = {};
    activeSession = false;
    dirty = false;
    loaded = true;
    return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("RST", READING_STATS_FILE_JSON, file)) {
    return false;
  }
  const bool loadedFromJson = JsonSettingsIO::loadReadingStats(*this, file);
  if (loadedFromJson) {
    activeSession = false;
    activeBookIndex = 0;
    activeAccumulatedMs = 0;
    dirty = false;
    lastSaveMs = millis();
    loaded = true;
  }
  return loadedFromJson;
}

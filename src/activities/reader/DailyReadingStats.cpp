#include "DailyReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace DailyReadingStats {
namespace {
// One current day and one midnight neighbor. No history array or heap allocation.
struct Cache {
  uint32_t day = UINT32_MAX;
  Counter counter{};
  uint32_t dirty = 0;
};
Cache cache[2];
void pathFor(char* path, size_t capacity, uint32_t day, const char* suffix = "") {
  snprintf(path, capacity, "%s/%05lu.bin%s", DIRECTORY, static_cast<unsigned long>(day), suffix);
}
uint32_t checksum(const uint8_t* bytes, uint32_t day) {
  uint32_t hash = 2166136261u ^ day;
  for (size_t i = 0; i < 9; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
uint32_t le32(const uint8_t* b) {
  return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
void put32(uint8_t* b, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) b[i] = value >> (i * 8);
}
bool save(Cache& c) {
  if (!c.dirty) return true;
  if (!write(c.day, c.counter)) return false;
  c.dirty = 0;
  return true;
}
}  // namespace

bool read(uint32_t day, Counter& counter) {
  counter = {};
  bool exists = false;
  bool valid = false;
  for (const char* suffix : {"", ".bak", ".tmp"}) {
    char path[64];
    pathFor(path, sizeof(path), day, suffix);
    if (!Storage.exists(path)) continue;
    exists = true;
    FsFile file;
    if (!Storage.openFileForRead("DailyStats", path, file)) return false;
    uint8_t data[13]{};
    const size_t size = file.fileSize();
    const int n = file.read(data, sizeof(data));
    file.close();
    if (size > sizeof(data) || (n > 0 && data[0] > 1)) {
      LOG_ERR("DailyStats", "Refusing newer daily counter: %s", path);
      return false;
    }
    if (size != sizeof(data) || n != sizeof(data) || data[0] != 1 || le32(data + 9) != checksum(data, day) ||
        le32(data + 5) > le32(data + 1))
      continue;
    counter.seconds = std::max(counter.seconds, le32(data + 1));
    counter.uploaded = std::max(counter.uploaded, le32(data + 5));
    valid = true;
  }
  if (exists && !valid) LOG_ERR("DailyStats", "No valid daily counter for day %lu", static_cast<unsigned long>(day));
  return !exists || valid;
}

bool write(uint32_t day, const Counter& counter) {
  ReadingStatsDate date;
  if (!readingStatsDateFromDayIndex(day, date) || counter.uploaded > counter.seconds) return false;
  // Never overwrite an unknown/corrupt format or lower a recovered checkpoint.
  Counter previous;
  if (!read(day, previous) || counter.seconds < previous.seconds) return false;
  if (!Storage.exists(DIRECTORY) && !Storage.mkdir(DIRECTORY)) {
    LOG_ERR("DailyStats", "Cannot create daily stats directory");
    return false;
  }
  char path[64], temp[64], backup[64];
  pathFor(path, sizeof(path), day);
  pathFor(temp, sizeof(temp), day, ".tmp");
  pathFor(backup, sizeof(backup), day, ".bak");
  if (Storage.exists(temp) && !Storage.remove(temp)) return false;
  uint8_t data[13]{};
  data[0] = 1;
  put32(data + 1, counter.seconds);
  put32(data + 5, std::max(counter.uploaded, previous.uploaded));
  put32(data + 9, checksum(data, day));
  FsFile file;
  if (!Storage.openFileForWrite("DailyStats", temp, file)) return false;
  const bool complete = file.write(data, sizeof(data)) == sizeof(data) && file.sync();
  const bool closed = file.close();
  if (!complete || !closed) {
    Storage.remove(temp);
    LOG_ERR("DailyStats", "Failed daily checkpoint write");
    return false;
  }
  if (Storage.exists(backup) && !Storage.remove(backup)) return false;
  if (Storage.exists(path) && !Storage.rename(path, backup)) return false;
  if (!Storage.rename(temp, path)) {
    LOG_ERR("DailyStats", "Failed daily checkpoint rename");
    // Keep the verified temp checkpoint: read() recovers it after reboot/retry.
    return false;
  }
  for (auto& c : cache)
    if (c.day == day) c.counter.uploaded = std::max(c.counter.uploaded, le32(data + 5));
  return true;
}

bool record(uint32_t day, uint32_t seconds) {
  Cache* chosen = nullptr;
  for (auto& c : cache)
    if (c.day == day) chosen = &c;
  if (!chosen) {
    chosen = cache[0].day == UINT32_MAX ? &cache[0] : &cache[1];
    if (!save(*chosen)) return false;
    Counter counter;
    if (!read(day, counter)) return false;
    *chosen = {day, counter, 0};
  }
  chosen->counter.seconds =
      UINT32_MAX - chosen->counter.seconds < seconds ? UINT32_MAX : chosen->counter.seconds + seconds;
  chosen->dirty += seconds;
  if (chosen->dirty >= 60 && !save(*chosen)) {
    LOG_ERR("DailyStats", "Checkpoint deferred; accepted seconds retained in RAM");
  }
  // Success means the seconds were accepted; flush() separately reports durability.
  return true;
}

bool flush() {
  bool ok = true;
  for (auto& c : cache)
    if (!save(c)) ok = false;
  return ok;
}

bool secondsFor(uint32_t day, uint32_t& seconds) {
  seconds = 0;
  for (const auto& c : cache) {
    if (c.day == day) {
      seconds = c.counter.seconds;
      return true;
    }
  }
  Counter counter;
  if (!read(day, counter)) return false;
  seconds = counter.seconds;
  return true;
}

Summary summarize(uint32_t today) {
  constexpr uint32_t days = 7;
  Summary summary;
  uint64_t total = 0;
  summary.hasSevenDayAverage = today + 1 >= days;
  for (uint32_t offset = 0; offset < days && offset <= today; ++offset) {
    uint32_t seconds = 0;
    if (!secondsFor(today - offset, seconds)) {
      LOG_ERR("DailyStats", "Cannot read daily counter for day %lu", static_cast<unsigned long>(today - offset));
      summary.hasSevenDayAverage = false;
      continue;
    }
    if (offset == 0) {
      summary.hasToday = true;
      summary.todaySeconds = seconds;
    }
    total += seconds;
  }
  summary.sevenDayAverageSeconds = static_cast<uint32_t>(total / days);
  return summary;
}

bool Session::accept(const ReadingStatsDateTime& end, uint8_t utcOffset, uint32_t seconds, uint32_t sessionSeconds) {
  bool ok = true;
  if (sessionSeconds >= 10) {
    uint8_t retained = 0;
    for (uint8_t i = 0; i < pendingCount; ++i) {
      if (!record(pending[i].day, pending[i].seconds)) {
        pending[retained++] = pending[i];
        ok = false;
      }
    }
    pendingCount = retained;
  }
  if (!seconds || !startTime.isValid() || !end.isValid() || offset != utcOffset || utcOffset > 104) return ok;
  const uint64_t start = uint64_t(readingStatsDayIndex(startTime.date)) * 86400 + startTime.hour * 3600u +
                         startTime.minute * 60u + startTime.second;
  const uint64_t finish =
      uint64_t(readingStatsDayIndex(end.date)) * 86400 + end.hour * 3600u + end.minute * 60u + end.second;
  // Clock corrections and unavailable clocks must not manufacture dated time.
  if (finish < start || finish - start + 2 < seconds || finish - start > uint64_t(seconds) + 2) return ok;
  uint64_t cursor = start;
  uint32_t remaining = seconds;
  while (remaining) {
    const uint32_t segment = std::min(remaining, 86400u - uint32_t(cursor % 86400));
    const uint32_t day = cursor / 86400;
    const bool deferred = sessionSeconds < 10 || !record(day, segment);
    if (deferred) {
      if (sessionSeconds >= 10) ok = false;
      uint8_t slot = 0;
      while (slot < pendingCount && pending[slot].day != day) ++slot;
      if (slot == pendingCount) {
        if (pendingCount >= 10) {
          LOG_ERR("DailyStats", "Pending day limit exceeded during storage failure");
          return false;
        }
        pending[pendingCount++] = {day, 0};
      }
      pending[slot].seconds =
          UINT32_MAX - pending[slot].seconds < segment ? UINT32_MAX : pending[slot].seconds + segment;
    }
    remaining -= segment;
    cursor += segment;
  }
  return ok;
}
}  // namespace DailyReadingStats

#pragma once
#include <cstdint>
#include <string>

// Per-book reading statistics, persisted by ReadingStatsStore.
struct BookReadingStats {
  uint16_t sessionCount = 0;         // Total times this book was opened
  uint32_t totalReadingSeconds = 0;  // Accumulated reading time in seconds
  uint32_t totalPagesTurned = 0;     // Total page-turn actions (forward + backward)
  bool isCompleted = false;          // Whether the user manually marked this book as finished

  // Loads stats from the canonical reading stats store.
  static BookReadingStats load(const std::string& cachePath);

  // Saves stats to the canonical reading stats store.
  void save(const std::string& cachePath) const;

  // Formats a duration in seconds into a human-readable string.
  // Output examples: "< 1 min", "45 min", "2h 30 min"
  static void formatDuration(uint32_t seconds, char* buf, size_t len);
};

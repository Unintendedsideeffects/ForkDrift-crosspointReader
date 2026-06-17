#pragma once
#include <cstdint>

// Cumulative reading statistics across all books, persisted by ReadingStatsStore.
struct GlobalReadingStats {
  uint32_t totalSessions = 0;        // Total book-open events across all books
  uint32_t totalReadingSeconds = 0;  // Accumulated reading time across all books
  uint32_t totalPagesTurned = 0;     // Total page-turn actions across all books
  uint32_t completedBooks = 0;       // Books manually marked as finished

  // Loads stats from the canonical reading stats store.
  static GlobalReadingStats load();

  // Saves stats to the canonical reading stats store.
  void save() const;
};

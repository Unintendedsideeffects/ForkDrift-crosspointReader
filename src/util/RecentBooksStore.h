#pragma once

#include <HalStorage.h>

#include <mutex>
#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

class RecentBooksStore;
namespace JsonSettingsIO {
bool loadRecentBooks(RecentBooksStore& store, const char* json);
bool loadRecentBooks(RecentBooksStore& store, HalFile& file);
}  // namespace JsonSettingsIO

class RecentBooksStore {
  // Static instance
  static RecentBooksStore instance;

  std::vector<RecentBook> recentBooks;
  // std::mutex (not a FreeRTOS semaphore) so the host-test and simulator
  // builds compile unchanged; on ESP32 it maps to a FreeRTOS mutex anyway.
  mutable std::mutex booksMutex;

  bool saveToFileUnlocked() const;

  friend bool JsonSettingsIO::loadRecentBooks(RecentBooksStore&, const char*);
  friend bool JsonSettingsIO::loadRecentBooks(RecentBooksStore&, HalFile&);

 public:
  ~RecentBooksStore() = default;

  // Get singleton instance
  static RecentBooksStore& getInstance() { return instance; }

  // Add a book to the recent list (moves to front if already exists)
  void addBook(const std::string& path, const std::string& title, const std::string& author,
               const std::string& coverBmpPath);

  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath);

  void updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  // Get the list of recent books (most recent first)
  // Cross-task callers must use getBooksSnapshot() instead.
  const std::vector<RecentBook>& getBooks() const { return recentBooks; }

  // Get a copy of the list of recent books under the lock for cross-task safety
  std::vector<RecentBook> getBooksSnapshot() const;

  // Get the count of recent books
  // Cross-task callers must use getBooksSnapshot() instead.
  int getCount() const { return static_cast<int>(recentBooks.size()); }

  bool saveToFile() const;

  bool loadFromFile();
  RecentBook getDataFromBook(std::string path) const;
};

// Helper macro to access recent books store
#define RECENT_BOOKS RecentBooksStore::getInstance()

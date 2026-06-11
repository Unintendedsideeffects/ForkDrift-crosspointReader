#pragma once

#include <mutex>
#include <string>
#include <vector>

struct LibraryShelfEntry {
  std::string title;
  std::string author;
  std::string href;
};

class LibraryShelfStore {
  static LibraryShelfStore instance;

  std::string serverName;
  std::vector<LibraryShelfEntry> entries;
  mutable std::mutex shelfMutex;

  bool saveToFileUnlocked() const;

 public:
  static LibraryShelfStore& getInstance() { return instance; }

  std::vector<LibraryShelfEntry> getSnapshot() const;
  void replaceEntries(const std::string& serverName, std::vector<LibraryShelfEntry> entries);
  bool loadFromFile();
};

#define LIBRARY_SHELF LibraryShelfStore::getInstance()

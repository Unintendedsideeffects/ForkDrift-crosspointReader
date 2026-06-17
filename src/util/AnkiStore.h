#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

namespace util {

struct AnkiCard {
  std::string front;
  std::string back;
  std::string context;  // e.g. the book title or surrounding text
  uint32_t timestamp;   // when it was added
  uint32_t readCount;   // SRS reading count
};

class AnkiStore {
 public:
  static AnkiStore& getInstance();

  // Load from SD card. Must be called before any other method.
  bool load();
  // Persist to SD card. Acquires SpiBusMutex internally; do not call while holding it.
  bool save() const;

  // Thread-safe snapshot copy for display activities.
  std::vector<AnkiCard> copyCards() const;

  // Thread-safe JSON serialization for the web API. Acquires mutex internally.
  void buildCardsJson(std::string& out) const;

  // Mutators — thread-safe but do NOT auto-save. Callers must call save() explicitly.
  void addCard(const AnkiCard& card);
  void removeCard(size_t index);
  void updateCardBack(size_t index, const std::string& back);
  void incrementCardReadCount(size_t index);
  void clear();

  size_t count() const;

 private:
  AnkiStore();
  AnkiStore(const AnkiStore&) = delete;
  AnkiStore& operator=(const AnkiStore&) = delete;

  std::vector<AnkiCard> cards;
  static constexpr const char* kFilePath = "/.crosspoint/anki_cards.json";

  mutable StaticSemaphore_t mutexBuf_;
  mutable SemaphoreHandle_t mutex_;
};

}  // namespace util

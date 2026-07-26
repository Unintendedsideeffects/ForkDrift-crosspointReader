#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct FlashcardCard {
  std::string key;
  std::string front;
  std::string back;
};

struct FlashcardCardProgress {
  std::string key;
  uint32_t attempts = 0;
  uint32_t correctCount = 0;
  uint32_t dueDayOrdinal = 0;
  uint16_t intervalDays = 0;
  bool failed = false;
};

struct FlashcardDeck {
  std::string deckId;
  std::string path;
  std::string title;
  std::vector<FlashcardCard> cards;
};

struct FlashcardSessionSummary {
  uint32_t totalReviewed = 0;
  uint32_t correctCount = 0;
  uint32_t failedCount = 0;
  uint32_t newSeenCount = 0;
};

enum class FlashcardStudyMode : uint8_t {
  All = 0,
  New,
  Due,
  Failed,
};

enum class FlashcardLoadStatus : uint8_t {
  Ok,
  Empty,
  Malformed,
  TooLarge,
  TooManyCards,
  TooManyColumns,
  RecordTooLarge,
  FieldTooLarge,
  IoError,
};

class FlashcardsStore {
 public:
  static constexpr size_t MAX_DECK_BYTES = 256 * 1024;
  static constexpr size_t MAX_CARDS = 512;
  static constexpr size_t MAX_COLUMNS = 16;
  static constexpr size_t MAX_RECORD_BYTES = 4096;
  static constexpr size_t MAX_FIELD_BYTES = 2048;
  static constexpr size_t MAX_PROGRESS_BYTES = 160 * 1024;
  static constexpr size_t MAX_DISCOVERED_DECKS = 64;

  // Storage/model layer adapted from CPR-Vortex: https://github.com/franssjz/cpr-vcodex
  static std::vector<FlashcardCard> parseCsvDeck(const std::string& csvContent);
  static FlashcardLoadStatus parseCsvDeckBounded(const std::string& csvContent, std::vector<FlashcardCard>& outCards);
  static std::string titleFromPath(const std::string& path);
  static bool isValidDeckPath(const std::string& path);
  static std::vector<std::string> listDecks(const std::string& directoryPath);
  static bool loadDeck(const std::string& path, FlashcardDeck& outDeck, std::string* outError = nullptr);
  static std::string getProgressFilePath(const std::string& deckPath);
  static bool loadDeckProgress(const FlashcardDeck& deck, std::vector<FlashcardCardProgress>& outProgress);
  static bool saveDeckProgress(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress);
  static void recordAnswer(FlashcardCardProgress& progress, int quality, uint32_t currentDay);
  static std::vector<size_t> buildStudyQueue(const FlashcardDeck& deck,
                                             const std::vector<FlashcardCardProgress>& progress,
                                             FlashcardStudyMode mode, uint32_t currentDay);
  static void updateSessionSummary(FlashcardSessionSummary& summary, bool success, bool isNew);
};

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

class FlashcardsStore {
 public:
  // Storage/model layer adapted from CPR-Vortex: https://github.com/franssjz/cpr-vcodex
  static std::vector<FlashcardCard> parseCsvDeck(const std::string& csvContent);
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

#include "FlashcardsStore.h"

#include <ArduinoJson.h>
#include <BookCachePath.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "util/PathUtils.h"

namespace {

std::string trimAscii(const std::string& value) {
  const size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

std::string fallbackCardKey(const std::string& front, const std::string& back) {
  uint32_t hash = 2166136261u;
  const std::string value = front + "\x1f" + back;
  for (const unsigned char ch : value) {
    hash ^= ch;
    hash *= 16777619u;
  }
  char buffer[9];
  std::snprintf(buffer, sizeof(buffer), "%08x", hash);
  return buffer;
}

std::vector<std::vector<std::string>> parseCsvRows(const std::string& csvContent) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool inQuotes = false;

  for (size_t i = 0; i < csvContent.size(); ++i) {
    const char ch = csvContent[i];
    if (inQuotes) {
      if (ch == '"' && i + 1 < csvContent.size() && csvContent[i + 1] == '"') {
        field.push_back('"');
        ++i;
      } else if (ch == '"') {
        inQuotes = false;
      } else {
        field.push_back(ch);
      }
      continue;
    }

    if (ch == '"') {
      inQuotes = true;
    } else if (ch == ',') {
      row.push_back(trimAscii(field));
      field.clear();
    } else if (ch == '\n') {
      row.push_back(trimAscii(field));
      rows.push_back(row);
      row.clear();
      field.clear();
    } else if (ch != '\r') {
      field.push_back(ch);
    }
  }

  if (!field.empty() || !row.empty()) {
    row.push_back(trimAscii(field));
    rows.push_back(row);
  }
  return rows;
}

}  // namespace

bool FlashcardsStore::isValidDeckPath(const std::string& path) {
  if (!PathUtils::isValidSdPath(path.c_str())) {
    return false;
  }
  const bool startsWithFlashcards = (path.size() >= 12 && path.compare(0, 12, "/flashcards/") == 0);
  const bool startsWithDecks = (path.size() >= 7 && path.compare(0, 7, "/decks/") == 0);
  if (!startsWithFlashcards && !startsWithDecks) {
    return false;
  }
  if (path.length() < 4) {
    return false;
  }
  std::string suffix = path.substr(path.length() - 4);
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
  return suffix == ".csv";
}

std::vector<FlashcardCard> FlashcardsStore::parseCsvDeck(const std::string& csvContent) {
  auto rows = parseCsvRows(csvContent);
  std::vector<FlashcardCard> cards;
  if (rows.empty()) {
    return cards;
  }

  int idColumn = -1;
  int frontColumn = 0;
  int backColumn = 1;
  bool hasHeader = false;
  for (int i = 0; i < static_cast<int>(rows.front().size()); ++i) {
    const std::string field = lowerAscii(rows.front()[i]);
    if (field == "id" || field == "card_id") {
      idColumn = i;
      hasHeader = true;
    } else if (field == "front" || field == "question") {
      frontColumn = i;
      hasHeader = true;
    } else if (field == "back" || field == "answer") {
      backColumn = i;
      hasHeader = true;
    }
  }
  if (hasHeader) {
    rows.erase(rows.begin());
  }

  const int requiredColumn = std::max(frontColumn, backColumn);
  for (const auto& row : rows) {
    if (static_cast<int>(row.size()) <= requiredColumn) {
      continue;
    }
    const std::string front = trimAscii(row[frontColumn]);
    const std::string back = trimAscii(row[backColumn]);
    if (front.empty() && back.empty()) {
      continue;
    }
    std::string key;
    if (idColumn >= 0 && idColumn < static_cast<int>(row.size())) {
      key = trimAscii(row[idColumn]);
    }
    if (key.empty()) {
      key = fallbackCardKey(front, back);
    }
    cards.push_back(FlashcardCard{key, front, back});
  }
  return cards;
}

std::vector<std::string> FlashcardsStore::listDecks(const std::string& directoryPath) {
  std::vector<std::string> decks;
  for (const auto& file : Storage.listFiles(directoryPath.c_str())) {
    const std::string filename = file.c_str();
    if (filename.size() <= 4 || lowerAscii(filename.substr(filename.size() - 4)) != ".csv") {
      continue;
    }
    std::string path = directoryPath;
    if (path.empty() || path.back() != '/') {
      path += "/";
    }
    decks.push_back(path + filename);
  }
  return decks;
}

bool FlashcardsStore::loadDeck(const std::string& path, FlashcardDeck& outDeck, std::string* outError) {
  if (!Storage.exists(path.c_str())) {
    if (outError) {
      *outError = "Deck file not found";
    }
    return false;
  }
  const String csv = Storage.readFile(path.c_str());
  if (csv.isEmpty()) {
    if (outError) {
      *outError = "Deck file empty";
    }
    return false;
  }
  auto cards = parseCsvDeck(csv.c_str());
  if (cards.empty()) {
    if (outError) {
      *outError = "No valid cards found";
    }
    return false;
  }

  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = filename.rfind('.');
  outDeck.path = path;
  outDeck.deckId = std::to_string(BookCachePath::stableHash(path));
  outDeck.title = dot == std::string::npos ? filename : filename.substr(0, dot);
  outDeck.cards = std::move(cards);
  return true;
}

std::string FlashcardsStore::getProgressFilePath(const std::string& deckPath) {
  return BookCachePath::build("/.crosspoint", "fc_", deckPath) + ".json";
}

bool FlashcardsStore::loadDeckProgress(const FlashcardDeck& deck, std::vector<FlashcardCardProgress>& outProgress) {
  outProgress.clear();
  const std::string path = getProgressFilePath(deck.path);
  JsonDocument doc;
  if (Storage.exists(path.c_str())) {
    const String json = Storage.readFile(path.c_str());
    if (!json.isEmpty()) {
      deserializeJson(doc, json.c_str());
    }
  }

  JsonArray saved = doc["cards"].as<JsonArray>();
  for (const auto& card : deck.cards) {
    FlashcardCardProgress progress;
    progress.key = card.key;
    for (JsonObject obj : saved) {
      if (std::string(obj["key"] | "") != card.key) {
        continue;
      }
      progress.attempts = obj["attempts"] | 0u;
      progress.correctCount = obj["correctCount"] | 0u;
      progress.dueDayOrdinal = obj["dueDayOrdinal"] | 0u;
      progress.intervalDays = obj["intervalDays"] | 0u;
      progress.failed = obj["failed"] | false;
      break;
    }
    outProgress.push_back(progress);
  }
  return true;
}

bool FlashcardsStore::saveDeckProgress(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress) {
  Storage.mkdir("/.crosspoint");
  JsonDocument doc;
  doc["deckId"] = deck.deckId;
  doc["path"] = deck.path;
  JsonArray cards = doc["cards"].to<JsonArray>();
  for (const auto& item : progress) {
    JsonObject obj = cards.add<JsonObject>();
    obj["key"] = item.key;
    obj["attempts"] = item.attempts;
    obj["correctCount"] = item.correctCount;
    obj["dueDayOrdinal"] = item.dueDayOrdinal;
    obj["intervalDays"] = item.intervalDays;
    obj["failed"] = item.failed;
  }
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(getProgressFilePath(deck.path).c_str(), json);
}

void FlashcardsStore::recordAnswer(FlashcardCardProgress& progress, int quality, uint32_t currentDay) {
  progress.attempts++;
  if (quality < 3) {
    progress.failed = true;
    progress.intervalDays = 0;
    progress.dueDayOrdinal = currentDay;
    return;
  }

  progress.correctCount++;
  progress.failed = false;
  if (progress.intervalDays == 0) {
    progress.intervalDays = 1;
  } else if (progress.intervalDays == 1) {
    progress.intervalDays = 3;
  } else {
    const float multiplier = quality >= 5 ? 2.5f : quality == 3 ? 1.4f : 2.0f;
    progress.intervalDays =
        std::min<uint16_t>(365, static_cast<uint16_t>(std::round(progress.intervalDays * multiplier)));
  }
  progress.dueDayOrdinal = currentDay + progress.intervalDays;
}

std::vector<size_t> FlashcardsStore::buildStudyQueue(const FlashcardDeck& deck,
                                                     const std::vector<FlashcardCardProgress>& progress,
                                                     FlashcardStudyMode mode, uint32_t currentDay) {
  std::vector<size_t> queue;
  const size_t count = std::min(deck.cards.size(), progress.size());
  for (size_t i = 0; i < count; ++i) {
    const auto& item = progress[i];
    const bool include =
        mode == FlashcardStudyMode::All || (mode == FlashcardStudyMode::New && item.attempts == 0) ||
        (mode == FlashcardStudyMode::Due && (item.attempts == 0 || item.failed || item.dueDayOrdinal <= currentDay)) ||
        (mode == FlashcardStudyMode::Failed && item.failed);
    if (include) {
      queue.push_back(i);
    }
  }
  return queue;
}

void FlashcardsStore::updateSessionSummary(FlashcardSessionSummary& summary, bool success, bool isNew) {
  summary.totalReviewed++;
  if (success) {
    summary.correctCount++;
  } else {
    summary.failedCount++;
  }
  if (isNew) {
    summary.newSeenCount++;
  }
}

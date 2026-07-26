#include "FlashcardsStore.h"

#include <ArduinoJson.h>
#include <BookCachePath.h>
#include <HalStorage.h>
#include <HeapGuard.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <unordered_map>

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

enum class CsvState : uint8_t { Unquoted, Quoted, QuoteSeen };

template <typename NextByte>
FlashcardLoadStatus parseCsvStream(NextByte&& nextByte, std::vector<FlashcardCard>& outCards) {
  std::vector<FlashcardCard> cards;
  std::vector<std::string> row;
  std::string field;
  row.reserve(4);
  field.reserve(128);

  int idColumn = -1;
  int frontColumn = 0;
  int backColumn = 1;
  bool firstRow = true;
  size_t consumed = 0;
  size_t recordBytes = 0;
  CsvState state = CsvState::Unquoted;

  auto finishField = [&]() -> FlashcardLoadStatus {
    if (row.size() >= FlashcardsStore::MAX_COLUMNS) return FlashcardLoadStatus::TooManyColumns;
    row.push_back(trimAscii(field));
    field.clear();
    return FlashcardLoadStatus::Ok;
  };

  auto finishRow = [&]() -> FlashcardLoadStatus {
    const auto fieldStatus = finishField();
    if (fieldStatus != FlashcardLoadStatus::Ok) return fieldStatus;

    if (firstRow) {
      bool hasHeader = false;
      for (int i = 0; i < static_cast<int>(row.size()); ++i) {
        const std::string name = lowerAscii(row[i]);
        if (name == "id" || name == "card_id") {
          idColumn = i;
          hasHeader = true;
        } else if (name == "front" || name == "question") {
          frontColumn = i;
          hasHeader = true;
        } else if (name == "back" || name == "answer") {
          backColumn = i;
          hasHeader = true;
        }
      }
      firstRow = false;
      if (hasHeader) {
        row.clear();
        return FlashcardLoadStatus::Ok;
      }
    }

    const int requiredColumn = std::max(frontColumn, backColumn);
    if (static_cast<int>(row.size()) > requiredColumn) {
      std::string front = trimAscii(row[frontColumn]);
      std::string back = trimAscii(row[backColumn]);
      if (!front.empty() || !back.empty()) {
        if (cards.size() >= FlashcardsStore::MAX_CARDS) return FlashcardLoadStatus::TooManyCards;
        std::string key;
        if (idColumn >= 0 && idColumn < static_cast<int>(row.size())) key = trimAscii(row[idColumn]);
        if (key.empty()) key = fallbackCardKey(front, back);
        cards.push_back({std::move(key), std::move(front), std::move(back)});
      }
    }
    row.clear();
    return FlashcardLoadStatus::Ok;
  };

  bool hasRecordData = false;
  for (;;) {
    const int raw = nextByte();
    if (raw < 0) break;
    const char ch = static_cast<char>(raw);
    if (++consumed > FlashcardsStore::MAX_DECK_BYTES) return FlashcardLoadStatus::TooLarge;
    if (++recordBytes > FlashcardsStore::MAX_RECORD_BYTES) return FlashcardLoadStatus::RecordTooLarge;
    hasRecordData = true;

    if (state == CsvState::Quoted) {
      if (ch == '"') {
        state = CsvState::QuoteSeen;
      } else {
        if (field.size() >= FlashcardsStore::MAX_FIELD_BYTES) return FlashcardLoadStatus::FieldTooLarge;
        field.push_back(ch);
      }
      continue;
    }
    if (state == CsvState::QuoteSeen && ch == '"') {
      if (field.size() >= FlashcardsStore::MAX_FIELD_BYTES) return FlashcardLoadStatus::FieldTooLarge;
      field.push_back('"');
      state = CsvState::Quoted;
      continue;
    }
    if (state == CsvState::QuoteSeen) state = CsvState::Unquoted;

    if (ch == '"') {
      state = CsvState::Quoted;
    } else if (ch == ',') {
      const auto status = finishField();
      if (status != FlashcardLoadStatus::Ok) return status;
    } else if (ch == '\n') {
      const auto status = finishRow();
      if (status != FlashcardLoadStatus::Ok) return status;
      recordBytes = 0;
      hasRecordData = false;
    } else if (ch != '\r') {
      if (field.size() >= FlashcardsStore::MAX_FIELD_BYTES) return FlashcardLoadStatus::FieldTooLarge;
      field.push_back(ch);
    }
  }

  if (state == CsvState::Quoted) return FlashcardLoadStatus::Malformed;
  if (hasRecordData || !field.empty() || !row.empty()) {
    const auto status = finishRow();
    if (status != FlashcardLoadStatus::Ok) return status;
  }
  if (cards.empty()) return FlashcardLoadStatus::Empty;
  outCards = std::move(cards);
  return FlashcardLoadStatus::Ok;
}

const char* statusMessage(const FlashcardLoadStatus status) {
  switch (status) {
    case FlashcardLoadStatus::Ok:
      return "";
    case FlashcardLoadStatus::Empty:
      return "No valid cards found";
    case FlashcardLoadStatus::Malformed:
      return "Malformed CSV quoting";
    case FlashcardLoadStatus::TooLarge:
      return "Deck file exceeds 256 KiB limit";
    case FlashcardLoadStatus::TooManyCards:
      return "Deck exceeds 512 card limit";
    case FlashcardLoadStatus::TooManyColumns:
      return "CSV record has too many columns";
    case FlashcardLoadStatus::RecordTooLarge:
      return "CSV record exceeds 4096 byte limit";
    case FlashcardLoadStatus::FieldTooLarge:
      return "CSV field exceeds 2048 byte limit";
    case FlashcardLoadStatus::IoError:
      return "Deck I/O error";
  }
  return "Deck load failed";
}

bool writeAll(HalFile& file, const char* data, const size_t length) {
  return length == 0 || file.write(reinterpret_cast<const uint8_t*>(data), length) == length;
}

bool writeJsonString(HalFile& file, const std::string& value) {
  if (!writeAll(file, "\"", 1)) return false;
  for (const unsigned char ch : value) {
    const char* escaped = nullptr;
    switch (ch) {
      case '"':
        escaped = "\\\"";
        break;
      case '\\':
        escaped = "\\\\";
        break;
      case '\n':
        escaped = "\\n";
        break;
      case '\r':
        escaped = "\\r";
        break;
      case '\t':
        escaped = "\\t";
        break;
      default:
        break;
    }
    if (escaped) {
      if (!writeAll(file, escaped, 2)) return false;
    } else if (ch >= 0x20) {
      const char byte = static_cast<char>(ch);
      if (!writeAll(file, &byte, 1)) return false;
    }
  }
  return writeAll(file, "\"", 1);
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
  std::vector<FlashcardCard> cards;
  parseCsvDeckBounded(csvContent, cards);
  return cards;
}

FlashcardLoadStatus FlashcardsStore::parseCsvDeckBounded(const std::string& csvContent,
                                                         std::vector<FlashcardCard>& outCards) {
  size_t offset = 0;
  return parseCsvStream(
      [&]() -> int {
        if (offset >= csvContent.size()) return -1;
        return static_cast<unsigned char>(csvContent[offset++]);
      },
      outCards);
}

std::string FlashcardsStore::titleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = filename.rfind('.');
  return dot == std::string::npos ? filename : filename.substr(0, dot);
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
    if (decks.size() >= MAX_DISCOVERED_DECKS) break;
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
  HalFile file;
  if (!Storage.openFileForRead("ANKI", path, file)) {
    if (outError) {
      *outError = "Deck I/O error";
    }
    return false;
  }
  if (file.size() == 0 || file.size() > MAX_DECK_BYTES) {
    if (outError) {
      *outError = file.size() == 0 ? "Deck file empty" : statusMessage(FlashcardLoadStatus::TooLarge);
    }
    file.close();
    return false;
  }
  if (heapguard::pressure() == heapguard::Pressure::Critical) {
    if (outError) *outError = "Insufficient heap to load deck";
    file.close();
    return false;
  }

  std::vector<FlashcardCard> cards;
  const auto status = parseCsvStream([&]() -> int { return file.read(); }, cards);
  file.close();
  if (status != FlashcardLoadStatus::Ok) {
    if (outError) *outError = statusMessage(status);
    return false;
  }

  FlashcardDeck loaded;
  loaded.path = path;
  loaded.deckId = std::to_string(BookCachePath::stableHash(path));
  loaded.title = titleFromPath(path);
  loaded.cards = std::move(cards);
  outDeck = std::move(loaded);
  return true;
}

std::string FlashcardsStore::getProgressFilePath(const std::string& deckPath) {
  return BookCachePath::build("/.crosspoint", "fc_", deckPath) + ".json";
}

bool FlashcardsStore::loadDeckProgress(const FlashcardDeck& deck, std::vector<FlashcardCardProgress>& outProgress) {
  const std::string path = getProgressFilePath(deck.path);
  JsonDocument doc;
  if (Storage.exists(path.c_str())) {
    HalFile file;
    if (!Storage.openFileForRead("ANKI", path, file) || file.size() > MAX_PROGRESS_BYTES) return false;
    const size_t fileSize = file.size();
    if (!heapguard::canAllocate(fileSize + 1)) {
      file.close();
      return false;
    }
    std::string json;
    json.reserve(fileSize + 1);
    uint8_t buffer[512];
    size_t totalRead = 0;
    while (file.available()) {
      const size_t count = file.read(buffer, sizeof(buffer));
      if (count == 0) break;
      json.append(reinterpret_cast<const char*>(buffer), count);
      totalRead += count;
    }
    file.close();
    if (totalRead != fileSize || deserializeJson(doc, json)) return false;
  }

  std::unordered_map<std::string, FlashcardCardProgress> savedByKey;
  JsonArray saved = doc["cards"].as<JsonArray>();
  if (saved.size() > MAX_CARDS) return false;
  if (!heapguard::canAllocate(saved.size() * (sizeof(FlashcardCardProgress) + 48))) return false;
  savedByKey.reserve(saved.size());
  for (JsonObject obj : saved) {
    const char* rawKey = obj["key"] | "";
    const std::string key = rawKey;
    if (key.empty() || key.size() > 128) return false;
    FlashcardCardProgress progress;
    progress.key = key;
    progress.attempts = obj["attempts"] | 0u;
    progress.correctCount = obj["correctCount"] | 0u;
    progress.dueDayOrdinal = obj["dueDayOrdinal"] | 0u;
    progress.intervalDays = obj["intervalDays"] | 0u;
    progress.failed = obj["failed"] | false;
    savedByKey[key] = std::move(progress);  // duplicate keys: last value wins
  }

  std::vector<FlashcardCardProgress> loaded;
  loaded.reserve(deck.cards.size());
  for (const auto& card : deck.cards) {
    FlashcardCardProgress progress;
    progress.key = card.key;
    const auto savedIt = savedByKey.find(card.key);
    if (savedIt != savedByKey.end()) progress = savedIt->second;
    loaded.push_back(std::move(progress));
  }
  outProgress = std::move(loaded);
  return true;
}

bool FlashcardsStore::saveDeckProgress(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress) {
  if (progress.size() > MAX_CARDS) return false;
  Storage.mkdir("/.crosspoint");
  const std::string path = getProgressFilePath(deck.path);
  const std::string tmpPath = path + ".tmp";
  const std::string bakPath = path + ".bak";
  HalFile file;
  if (!Storage.openFileForWrite("ANKI", tmpPath, file)) return false;

  bool ok = writeAll(file, "{\"deckId\":", 10) && writeJsonString(file, deck.deckId) &&
            writeAll(file, ",\"path\":", 8) && writeJsonString(file, deck.path) && writeAll(file, ",\"cards\":[", 10);
  char numbers[192];
  for (size_t i = 0; ok && i < progress.size(); ++i) {
    const auto& item = progress[i];
    if (item.key.empty() || item.key.size() > 128) {
      ok = false;
      break;
    }
    if (i > 0) ok = writeAll(file, ",", 1);
    ok = ok && writeAll(file, "{\"key\":", 7) && writeJsonString(file, item.key);
    const int length =
        std::snprintf(numbers, sizeof(numbers),
                      ",\"attempts\":%lu,\"correctCount\":%lu,\"dueDayOrdinal\":%lu,"
                      "\"intervalDays\":%u,\"failed\":%s}",
                      static_cast<unsigned long>(item.attempts), static_cast<unsigned long>(item.correctCount),
                      static_cast<unsigned long>(item.dueDayOrdinal), static_cast<unsigned int>(item.intervalDays),
                      item.failed ? "true" : "false");
    ok = ok && length > 0 && static_cast<size_t>(length) < sizeof(numbers) &&
         writeAll(file, numbers, static_cast<size_t>(length));
  }
  ok = ok && writeAll(file, "]}", 2);
  file.close();
  if (!ok) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  Storage.remove(bakPath.c_str());
  const bool hadPrevious = Storage.exists(path.c_str());
  if (hadPrevious && !Storage.rename(path.c_str(), bakPath.c_str())) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    if (hadPrevious) Storage.rename(bakPath.c_str(), path.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(bakPath.c_str());
  return true;
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

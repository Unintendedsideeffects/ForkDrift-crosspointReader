#include "AnkiStore.h"

#include <ArduinoJson.h>
#include <FsFileJsonReader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SpiBusMutex.h>

namespace util {
namespace {
constexpr size_t kMaxAnkiCardsFileBytes = 256u * 1024u;
constexpr size_t kMaxAnkiCards = 1024;
}  // namespace

AnkiStore::AnkiStore() : mutex_(xSemaphoreCreateMutexStatic(&mutexBuf_)) {}

AnkiStore& AnkiStore::getInstance() {
  static AnkiStore instance;
  return instance;
}

bool AnkiStore::load() {
  SpiBusMutex::Guard spiGuard;

  if (!Storage.exists(kFilePath)) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    cards.clear();
    xSemaphoreGive(mutex_);
    return true;
  }

  HalFile file;
  if (!Storage.openFileForRead("ANKI", kFilePath, file)) {
    return false;
  }
  if (file.size() > kMaxAnkiCardsFileBytes) {
    LOG_ERR("ANKI", "Cards file too large: %u bytes", static_cast<unsigned>(file.size()));
    file.close();
    return false;
  }

  // Parse outside the mutex — JSON parse is CPU-bound, not a shared-state concern.
  JsonDocument doc;
  FsFileJsonReader reader(file);
  const DeserializationError error = deserializeJson(doc, reader);
  file.close();
  if (error) {
    LOG_ERR("ANKI", "Failed to parse cards JSON: %s", error.c_str());
    return false;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  cards.clear();
  JsonArray arr = doc.as<JsonArray>();
  cards.reserve(arr.size() < kMaxAnkiCards ? arr.size() : kMaxAnkiCards);
  for (JsonObject obj : arr) {
    if (cards.size() >= kMaxAnkiCards) {
      break;
    }
    AnkiCard card;
    card.front = obj["f"] | "";
    card.back = obj["b"] | "";
    card.context = obj["c"] | "";
    card.timestamp = obj["t"] | 0;
    card.readCount = obj["r"] | 0;
    cards.push_back(std::move(card));
  }
  const size_t loaded = cards.size();
  xSemaphoreGive(mutex_);

  LOG_INF("ANKI", "Loaded %zu cards", loaded);
  return true;
}

bool AnkiStore::save() const {
  // Serialize under mutex so we get a consistent snapshot, then do SD I/O.
  String json;
  size_t count;
  {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& card : cards) {
      JsonObject obj = arr.add<JsonObject>();
      obj["f"] = card.front;
      obj["b"] = card.back;
      obj["c"] = card.context;
      obj["t"] = card.timestamp;
      obj["r"] = card.readCount;
    }
    count = cards.size();
    serializeJson(doc, json);
    xSemaphoreGive(mutex_);
  }

  SpiBusMutex::Guard spiGuard;
  Storage.mkdir("/.crosspoint");
  if (Storage.writeFile(kFilePath, json)) {
    LOG_INF("ANKI", "Saved %zu cards", count);
    return true;
  }

  LOG_ERR("ANKI", "Failed to save cards to %s", kFilePath);
  return false;
}

std::vector<AnkiCard> AnkiStore::copyCards() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<AnkiCard> snapshot = cards;
  xSemaphoreGive(mutex_);
  return snapshot;
}

void AnkiStore::buildCardsJson(std::string& out) const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& card : cards) {
    JsonObject obj = arr.add<JsonObject>();
    obj["front"] = card.front;
    obj["back"] = card.back;
    obj["context"] = card.context;
    obj["timestamp"] = card.timestamp;
    obj["readCount"] = card.readCount;
  }
  String json;
  serializeJson(doc, json);
  xSemaphoreGive(mutex_);
  out = json.c_str();
}

void AnkiStore::addCard(const AnkiCard& card) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cards.push_back(card);
  xSemaphoreGive(mutex_);
}

void AnkiStore::removeCard(size_t index) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (index < cards.size()) {
    cards.erase(cards.begin() + index);
  }
  xSemaphoreGive(mutex_);
}

void AnkiStore::updateCardBack(size_t index, const std::string& back) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (index < cards.size()) {
    cards[index].back = back;
  }
  xSemaphoreGive(mutex_);
}

void AnkiStore::incrementCardReadCount(size_t index) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (index < cards.size()) {
    cards[index].readCount++;
  }
  xSemaphoreGive(mutex_);
}

void AnkiStore::clear() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  cards.clear();
  xSemaphoreGive(mutex_);
}

size_t AnkiStore::count() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t n = cards.size();
  xSemaphoreGive(mutex_);
  return n;
}

}  // namespace util

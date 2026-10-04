#ifdef SIMULATOR
#include "StatsUploadSmokeTest.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/reader/DailyReadingStats.h"
#include "activities/reader/StatsUploadPayload.h"
#include "network/ReadingSyncUpload.h"
#include "network/StatsUploadClient.h"

namespace {
void require(bool ok, const char* what) {
  if (!ok) {
    LOG_ERR("SMOKE", "Stats upload contract failed: %s", what);
    std::_Exit(2);
  }
}
}  // namespace

void verifyStatsUploadContract() {
  // Host-only buffers: firmware uses one reusable heap buffer in its activity.
  std::vector<char> payload(StatsUploadPayload::CAPACITY);
  GlobalReadingStats global;
  require(!GlobalReadingStats::loadForUpload(global), "missing global stats must not export zeros");
  global.totalSessions = 12;
  global.totalReadingSeconds = 3456;
  global.readingHistoryAnchorDay = 9769;
  global.readingHistoryBits[0] = 7;
  global.save();
  GlobalReadingStats loaded;
  require(GlobalReadingStats::loadForUpload(loaded) && loaded.totalReadingSeconds == 3456, "checked global read");
  require(StatsUploadPayload::global(payload.data(), payload.size(), "crossink-test", loaded), "global serialization");
  JsonDocument json;
  require(!deserializeJson(json, payload.data()), "global JSON");
  require(json["v"] == 5 && json["seconds"] == 3456 && json["tod"].size() == 4 && json["dow"].size() == 7,
          "global fields");
  require(strlen(json["history_b64"].as<const char*>()) == 124 &&
              std::strncmp(json["history_b64"].as<const char*>(), "BwAA", 4) == 0,
          "calendar bit order");
  // Every count at its maximum still fits the fixed request budget.
  global.totalSessions = global.totalReadingSeconds = global.totalPagesTurned = global.completedBooks = UINT32_MAX;
  global.timeOfDaySeconds.fill(UINT32_MAX);
  global.dayOfWeekSeconds.fill(UINT32_MAX);
  require(StatsUploadPayload::global(payload.data(), payload.size(), "crossink-test", global),
          "maximum global payload");
  require(!StatsUploadPayload::global(payload.data(), 20, "crossink-test", global), "buffer overflow rejected");
  require(!StatsUploadPayload::global(payload.data(), payload.size(), "bad\"id", global), "unsafe ID rejected");
  BookReadingStats book;
  book.startDate = {2000, 1, 1};
  book.finishedDate = {2099, 12, 31};
  book.isCompleted = true;
  book.sessionCount = UINT16_MAX;
  book.totalReadingSeconds = book.totalPagesTurned = book.estimatedTimeLeftSeconds = UINT32_MAX;
  book.timeOfDaySeconds.fill(UINT32_MAX);
  book.dayOfWeekSeconds.fill(UINT32_MAX);
  constexpr char DOCUMENT[] = "01b729228ded144417191bebc5a17445";
  require(StatsUploadPayload::book(payload.data(), payload.size(), "crossink-test", DOCUMENT, book),
          "maximum book payload");
  require(!deserializeJson(json, payload.data()), "book JSON");
  const auto item = json["items"][0];
  require(item["start_date"].as<uint64_t>() == 946684800ULL && item["finished_date"].as<uint64_t>() == 4102358400ULL,
          "dates remain UTC beyond 2038");
  require(item["completed"].is<bool>() && item["completed"] == true, "boolean fields");
  // Do not leave fixture counters in the normal app-flow smoke session.
  Storage.remove("/.crosspoint/global_stats.bin");
  Storage.remove("/.crosspoint/global_stats.bin.bak");

  const char* server = std::getenv("CROSSINK_STATS_TEST_SERVER");
  if (server) {
    const char* user = std::getenv("CROSSINK_STATS_TEST_USER");
    const char* password = std::getenv("CROSSINK_STATS_TEST_PASSWORD");
    require(user && password, "explicit test credentials required");
    KOREADER_STORE.setCredentials(user, password);
    KOREADER_STORE.setServerUrl(server);
    if (std::getenv("CROSSINK_STATS_TEST_EMPTY_LIBRARY")) {
      global = {};
      global.save();
      if (std::getenv("CROSSINK_STATS_TEST_DAILY")) {
        require(DailyReadingStats::record(9769, 61) && DailyReadingStats::flush(), "manual daily fixture");
      }
      return;  // The main smoke state machine drives the actual activity.
    }
    StatsUploadClient client;
    char id[32];
    require(StatsUploadClient::deviceId(id, sizeof(id)), "simulator identity");
    // Zero snapshots make an explicitly requested live test non-inflating.
    global = {};
    book = {};
    require(StatsUploadPayload::global(payload.data(), payload.size(), id, global), "test global payload");
    const char* expected = std::getenv("CROSSINK_STATS_TEST_ERROR");
    auto result = client.put("/api/v1/stats/global", payload.data(), false);
    if (expected) {
      const auto wanted = static_cast<StatsUploadClient::Result>(std::atoi(expected));
      require(result == wanted, "expected transport failure classification");
    } else {
      require(result == StatsUploadClient::Result::Ok, "global upload");
      if (std::getenv("CROSSINK_STATS_TEST_DAILY")) {
        SETTINGS.trackReadingStats = 1;
        global.save();
        require(DailyReadingStats::record(9769, 61), "daily fixture");
        if (std::getenv("CROSSINK_STATS_TEST_DAILY_NO_ACK")) {
          require(ReadingSyncUpload::globalStats() == StatsUploadClient::Result::InvalidResponse,
                  "old server daily ack missing");
          DailyReadingStats::Counter pending;
          require(DailyReadingStats::read(9769, pending) && pending.seconds == 61 && pending.uploaded == 0,
                  "old server preserves pending daily history");
          LOG_INF("SMOKE", "Missing daily acknowledgment keeps history retryable");
          std::_Exit(0);
        }
        require(ReadingSyncUpload::globalStats() == StatsUploadClient::Result::Ok, "daily upload");
        require(ReadingSyncUpload::globalStats() == StatsUploadClient::Result::Ok, "daily retry");
        DailyReadingStats::Counter counter;
        require(DailyReadingStats::read(9769, counter) && counter.seconds == 61 && counter.uploaded == 61, "daily ack");
        require(DailyReadingStats::record(9769, 9), "more daily reading");
        require(ReadingSyncUpload::globalStats() == StatsUploadClient::Result::Ok, "incremental daily upload");
        require(DailyReadingStats::read(9769, counter) && counter.seconds == 70 && counter.uploaded == 70,
                "incremental daily ack");
      }
      require(client.put("/api/v1/stats/global", payload.data(), false) == StatsUploadClient::Result::Ok,
              "global retry");
      require(StatsUploadPayload::book(payload.data(), payload.size(), id, DOCUMENT, book), "test book payload");
      require(client.put("/api/v1/stats/books", payload.data(), true) == StatsUploadClient::Result::Ok, "book upload");
      require(client.put("/api/v1/stats/books", payload.data(), true) == StatsUploadClient::Result::Ok, "book retry");
    }
    LOG_INF("SMOKE", "Stats upload transport smoke passed");
    std::_Exit(0);
  }
  LOG_INF("SMOKE", "Stats upload serialization and storage smoke passed");
}
#endif

#ifdef SIMULATOR
#include "ReadingUploadSmokeTest.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <set>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/GlobalReadingStats.h"
#include "network/ClippingsUpload.h"
#include "network/ReadingSyncUpload.h"
#include "util/FolderBookIterator.h"

namespace {
void require(bool ok, const char* what) {
  if (!ok) {
    LOG_ERR("SMOKE", "Reading upload contract failed: %s", what);
    std::_Exit(2);
  }
}
}  // namespace

bool prepareReadingUploadSmokeTest() {
  const char* server = std::getenv("CROSSINK_READING_TEST_SERVER");
  if (!server) return false;
  require(!KOREADER_STORE.getSyncStats() && !KOREADER_STORE.getSyncClippings(), "extensions default off");
  KOREADER_STORE.setCredentials("smoke", "smoke-password");
  KOREADER_STORE.setServerUrl(server);
  KOREADER_STORE.setSendMetadata(true);
  KOREADER_STORE.setSyncBehavior(std::getenv("CROSSINK_READING_TEST_ASK") ? KOReaderSyncBehavior::ASK_EVERY_TIME
                                                                          : KOReaderSyncBehavior::SMART);
  KOREADER_STORE.setSyncStats(!std::getenv("CROSSINK_READING_TEST_DISABLED"));
  KOREADER_STORE.setSyncClippings(!std::getenv("CROSSINK_READING_TEST_DISABLED"));
  SETTINGS.trackReadingStats = 1;

  std::set<std::string> paths;
  FolderBookIterator iterator("/read/");
  for (int i = 0; i < 100; ++i) {
    std::string path;
    const auto result = iterator.next(path);
    require(result != FolderBookIterator::Result::Error, "folder traversal");
    if (result == FolderBookIterator::Result::Done) break;
    if (result == FolderBookIterator::Result::Book) require(paths.insert(path).second, "no duplicate paths");
    require(i < 99, "bounded folder traversal");
  }
  require(paths == std::set<std::string>{"/read/first.epub", "/read/sub/second.EPUB", "/read/unread.epub"},
          "recursive scope and hidden/non-EPUB filtering");
  FolderBookIterator missing("/not-present");
  std::string unused;
  require(missing.next(unused) == FolderBookIterator::Result::Error, "missing folder fails");
  for (const auto* path : {"/read/first.epub", "/read/sub/second.EPUB"}) {
    auto epub = std::make_shared<Epub>(path, "/.crosspoint");
    epub->setupCacheDir();
    require(epub->load(true, true), "fixture EPUB metadata");
    require(EpubReaderUtils::saveProgress(*epub, 0, 2, 10), "fixture saved progress");
    BookReadingStats stats;
    stats.sessionCount = 1;
    stats.totalReadingSeconds = 60;
    require(stats.save(epub->getCachePath()), "fixture saved stats");
  }
  KOReaderProgress progress;
  require(ReadingSyncUpload::prepareProgress("/read/first.epub", progress), "saved progress maps");
  require(progress.percentage > 0 && progress.percentage < 1 && !progress.progress.empty() && progress.metadata,
          "position and metadata preserved");
  JsonDocument expected;
  expected["document"] = progress.document;
  expected["progress"] = progress.progress;
  expected["percentage"] = progress.percentage;
  expected["device"] = "remote-test";
  expected["device_id"] = "remote-test";
  expected["timestamp"] = 1;
  std::string serialized;
  serializeJson(expected, serialized);
  require(Storage.writeFile("/expected-progress.json", serialized.c_str()), "expected position fixture");
  {
    Epub epub("/read/first.epub", "/.crosspoint");
    require(Storage.writeFile("/first-cache.txt", epub.getCachePath().c_str()), "cache path fixture");
    require(EpubReaderUtils::saveProgress(epub, 0, 7, 10), "remote progress fixture");
    require(ReadingSyncUpload::prepareProgress("/read/first.epub", progress), "remote progress maps");
    expected["progress"] = progress.progress;
    expected["percentage"] = progress.percentage;
    serialized.clear();
    serializeJson(expected, serialized);
    require(Storage.writeFile("/remote-progress.json", serialized.c_str()), "remote position fixture");
    require(EpubReaderUtils::saveProgress(epub, 0, 2, 10), "restore local fixture");
  }
  require(!ReadingSyncUpload::prepareProgress("/read/unread.epub", progress), "unread book skipped");
  if (std::getenv("CROSSINK_READING_TEST_MISSING_CACHE")) {
    Epub epub("/read/first.epub", "/.crosspoint");
    require(Storage.remove((epub.getCachePath() + "/book.bin").c_str()), "remove cached metadata");
  }
  if (std::getenv("CROSSINK_READING_TEST_INVALID_BOOK")) {
    Epub epub("/read/first.epub", "/.crosspoint");
    require(EpubReaderUtils::saveProgress(epub, 999, 2, 10), "invalid saved chapter fixture");
  }
  GlobalReadingStats global;
  global.totalReadingSeconds = 120;
  global.save();
  ClippingStore store;
  require(store.loadForBook("/read/first.epub", "Fixture", "Writer", "epub"), "clipping store");
  require(store.addClipping(0, 2, 2, 10, 1, 3, 4, "Chapter", UINT16_MAX, "Quoted \"text\" — café\nnext line",
                            UINT16_MAX, 1234) == ClippingStore::AddResult::Added,
          "clipping fixture");
  store.unload();
  if (std::getenv("CROSSINK_READING_TEST_SKIP_CLIPPINGS")) {
    require(store.loadForBook("/read/sub/second.EPUB", "Fixture", "Writer", "epub"), "second clipping store");
    require(store.addClipping(0, 2, 2, 10, 1, 3, 4, "Chapter", UINT16_MAX, "Second book quote", UINT16_MAX, 1234) ==
                ClippingStore::AddResult::Added,
            "second clipping fixture");
    store.unload();
  }
  require(clippingUnixTimestamp(1234) == 0 && clippingUnixTimestamp(1752300000) == 1752300000,
          "uptime is unknown, UTC timestamps preserved");
  Clipping saved;
  std::string quote;
  bool done = false;
  require(
      ClippingStore::readForUpload("/read/first.epub", 0, saved, quote, done) && !done && saved.timestamp >= 946684800U,
      "new clipping uses calendar time");
  char id[17];
  require(ClippingsUpload::identity(1752300000, "hello", id) && std::strcmp(id, "00dd471ed33eb756") == 0,
          "SHA256 server identity vector");
  // Persisted history is independent of whether a saved position can be mapped.
  if (const char* variant = std::getenv("CROSSINK_READING_TEST_PROGRESS_VARIANT")) {
    for (const auto* path : {"/read/first.epub", "/read/sub/second.EPUB"}) {
      Epub epub(path, "/.crosspoint");
      const std::string primary = epub.getCachePath() + "/progress.bin";
      if (std::strcmp(variant, "zero") == 0 || std::strcmp(variant, "past-end") == 0) {
        require(EpubReaderUtils::saveProgress(epub, 0, 2, std::strcmp(variant, "zero") == 0 ? 0 : 2),
                "unusable saved page count fixture");
      } else if (std::strcmp(variant, "legacy4") == 0) {
        auto file = Storage.open(primary.c_str(), O_WRONLY | O_TRUNC);
        const uint8_t data[4] = {0, 0, 2, 0};
        require(file && file.write(data, sizeof(data)) == sizeof(data), "legacy progress fixture");
        file.close();
      } else if (std::strcmp(variant, "missing") == 0) {
        require(Storage.remove(primary.c_str()), "remove saved progress fixture");
        Storage.remove((primary + ".bak").c_str());
      }
    }
  }
  if (std::getenv("CROSSINK_READING_TEST_NO_STATS")) {
    for (const auto* path : {"/read/first.epub", "/read/sub/second.EPUB"}) {
      Epub epub(path, "/.crosspoint");
      require(BookReadingStats::remove(epub.getCachePath()), "remove stats fixture");
    }
    Storage.remove("/.crosspoint/global_stats.bin");
    Storage.remove("/.crosspoint/global_stats.bin.bak");
  }
  if (std::getenv("CROSSINK_READING_TEST_TRACKING_DISABLED")) SETTINGS.trackReadingStats = 0;
  if (std::getenv("CROSSINK_READING_TEST_EMPTY_FOLDER")) {
    for (const auto* path : {"/read/first.epub", "/read/sub/second.EPUB", "/read/unread.epub"})
      require(Storage.remove(path), "empty folder fixture");
  }
  LOG_INF("SMOKE", "Reading upload fixtures and mapping passed");
  return true;
}
#endif

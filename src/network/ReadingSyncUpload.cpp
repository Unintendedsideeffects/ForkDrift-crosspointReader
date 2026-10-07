#include "ReadingSyncUpload.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <Memory.h>
#include <ProgressMapper.h>
#include <Xtc.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ClippingsUpload.h"
#include "CrossPointSettings.h"
#include "activities/reader/BookStatsTracking.h"
#include "activities/reader/DailyReadingStats.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/StatsUploadPayload.h"

bool ReadingSyncUpload::prepareProgress(const std::string& path, KOReaderProgress& progress) {
  if (!FsHelpers::hasEpubExtension(path)) return false;
  // ProgressMapper requires shared ownership. Release the whole EPUB before TLS.
  std::shared_ptr<Epub> epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("ReadingSync", "Cannot allocate EPUB for saved progress");
    return false;
  }
  EpubReaderUtils::Progress saved;
  if (!EpubReaderUtils::loadProgress(*epub, saved) ||
      !epub->load(false, true, Epub::XLocationLoadMode::Immediate, true) || saved.spineIndex < 0 ||
      saved.spineIndex >= epub->getSpineItemsCount() || !saved.hasPageCount || saved.pageCount < 1 ||
      saved.pageNumber >= saved.pageCount) {
    LOG_DBG("ReadingSync", "Skipping book without usable saved progress: %s", path.c_str());
    return false;
  }
  CrossPointPosition position{saved.spineIndex, saved.pageNumber, saved.pageCount};
  position.visibleTextOffset = saved.visibleTextOffset;
  position.hasVisibleTextOffset = saved.hasVisibleTextOffset;
  const auto method = KOREADER_STORE.getMatchMethod();
  const auto mapped =
      ProgressMapper::toKOReader(epub, position,
                                 method == DocumentMatchMethod::FILENAME ? PositionCoordinateSpace::SourceDocument
                                                                         : PositionCoordinateSpace::CurrentDocument);
  if (!mapped.valid || mapped.xpath.empty() || !std::isfinite(mapped.percentage) || mapped.percentage < 0 ||
      mapped.percentage > 1)
    return false;
  progress = {};
  progress.document = method == DocumentMatchMethod::FILENAME ? KOReaderDocumentId::calculateFromFilename(path)
                                                              : KOReaderDocumentId::calculate(path);
  if (progress.document.empty()) return false;
  progress.progress = mapped.xpath;
  progress.percentage = mapped.percentage;
  progress.device = SETTINGS.getEffectiveDeviceName();
  if (KOREADER_STORE.getSendMetadata()) {
    progress.metadata = KOReaderMetadata{path.substr(path.find_last_of('/') + 1), epub->getTitle(), epub->getAuthor()};
  }
  if (KOREADER_STORE.usesCrossPointSyncServer()) {
    KOReaderRichPosition rich;
    rich.pctQ = static_cast<uint32_t>(mapped.percentage * 1000000.0f + 0.5f);
    rich.spineIndex = saved.spineIndex;
    rich.pageNumber = saved.pageNumber;
    rich.totalPages = saved.pageCount;
    rich.xpath = mapped.xpath;
    progress.position = std::move(rich);
  }
  return true;
}

void ReadingSyncUpload::refreshServerSupport() {
  if (!KOREADER_STORE.needsServerProbe()) return;
  StatsUploadClient client;
  client.probe();
}

StatsUploadClient::Result ReadingSyncUpload::globalStats() {
  using Result = StatsUploadClient::Result;
  if (!SETTINGS.shouldTrackReadingStats()) return Result::Skipped;
  // Existing fixed 1536-byte upload buffer stays off the small task stack.
  auto payload = makeUniqueNoThrow<char[]>(StatsUploadPayload::CAPACITY);
  char deviceId[32];
  if (!payload || !StatsUploadClient::deviceId(deviceId, sizeof(deviceId))) return Result::LowMemory;
  StatsUploadClient client;
  return globalStats(client, payload.get(), StatsUploadPayload::CAPACITY, deviceId);
}

StatsUploadClient::Result ReadingSyncUpload::globalStats(StatsUploadClient& client, char* payload, size_t capacity,
                                                         const char* deviceId) {
  using Result = StatsUploadClient::Result;
  if (!SETTINGS.shouldTrackReadingStats()) return Result::Skipped;
  GlobalReadingStats global;
  if (!GlobalReadingStats::loadForUpload(global)) return Result::Skipped;
  if (!StatsUploadPayload::global(payload, capacity, deviceId, global)) return Result::InvalidResponse;
  auto result = client.put("/api/v1/stats/global", payload, false);
  if (result != Result::Ok) return result;
  if (!DailyReadingStats::flush()) return Result::InvalidResponse;
  if (!Storage.exists(DailyReadingStats::DIRECTORY)) return Result::Ok;
  FsFile dir = Storage.open(DailyReadingStats::DIRECTORY);
  if (!dir) {
    LOG_ERR("ReadingSync", "Cannot open daily reading history");
    return dir.allocationFailed() ? Result::LowMemory : Result::Server;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return Result::InvalidResponse;
  }
  // Stream one day at a time. RAM and request size stay fixed even after years offline.
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    char name[32]{};
    const size_t nameLength = file.getName(name, sizeof(name));
    const bool regular = !file.isDirectory();
    file.close();
    if (regular && nameLength == 0) {
      result = Result::Server;
      break;
    }
    unsigned day = 0;
    const size_t length = strlen(name);
    bool digits = (length == 9 && strcmp(name + 5, ".bin") == 0) ||
                  (length == 13 && (strcmp(name + 5, ".bin.bak") == 0 || strcmp(name + 5, ".bin.tmp") == 0));
    for (size_t i = 0; digits && i < 5; ++i) {
      digits = name[i] >= '0' && name[i] <= '9';
      day = day * 10 + unsigned(name[i] - '0');
    }
    if (!regular || !digits) continue;
    DailyReadingStats::Counter counter;
    if (!DailyReadingStats::read(day, counter)) {
      result = Result::InvalidResponse;
      break;
    }
    if (counter.uploaded == counter.seconds) continue;
    if (!StatsUploadPayload::global(payload, capacity, deviceId, global, day, counter.seconds)) {
      result = Result::InvalidResponse;
      break;
    }
    result = client.put("/api/v1/stats/global", payload, false, true);
    if (result != Result::Ok) break;
    counter.uploaded = counter.seconds;
    if (!DailyReadingStats::write(day, counter)) {
      result = Result::InvalidResponse;
      break;
    }
  }
  if (result == Result::Ok && dir.allocationFailed()) result = Result::LowMemory;
#ifndef SIMULATOR
  if (result == Result::Ok && dir.iterationFailed()) result = Result::Server;
#endif
  if (result != Result::Ok) LOG_ERR("ReadingSync", "Daily counter upload incomplete");
  dir.close();
  return result;
}

StatsUploadClient::Result ReadingSyncUpload::stats(const std::string& path, const std::string& document) {
  using Result = StatsUploadClient::Result;
  if (!SETTINGS.shouldTrackReadingStats()) return Result::Skipped;
  // XTC books keep stats in their own cache folder; they have no KOReader position.
  const auto cache = FsHelpers::hasXtcExtension(path) ? Xtc(path, "/.crosspoint").getCachePath()
                                                      : Epub::resolveCachePathForFilePath(path, "/.crosspoint");
  BookReadingStats book;
  if (!BookStatsTracking::isEnabled(cache) || !BookReadingStats::loadForUpload(cache, book)) return Result::Skipped;
  auto payload = makeUniqueNoThrow<char[]>(StatsUploadPayload::CAPACITY);
  char deviceId[32];
  if (!payload || !StatsUploadClient::deviceId(deviceId, sizeof(deviceId))) return Result::LowMemory;
  if (!StatsUploadPayload::book(payload.get(), StatsUploadPayload::CAPACITY, deviceId, document.c_str(), book))
    return Result::InvalidResponse;
  StatsUploadClient client;
  return client.put("/api/v1/stats/books", payload.get(), true);
}

ReadingSyncUpload::ExtrasResult ReadingSyncUpload::extras(const std::string& path, const std::string& document) {
  ExtrasResult result;
  if (KOREADER_STORE.getSyncStats()) result.stats = stats(path, document);
  // These independent snapshots must still be attempted after a stats failure.
  // Clippings are EPUB-only; XTC books never save any.
  if (KOREADER_STORE.getSyncClippings() && FsHelpers::hasEpubExtension(path))
    result.clippings = ClippingsUpload::upload(path, document);
  LOG_INF("ReadingSync", "Extras: stats=%d clippings=%d", static_cast<int>(result.stats),
          static_cast<int>(result.clippings));
  return result;
}

namespace {
const char* unavailableLabel(const bool clippings) {
  if (KOREADER_STORE.getServerSupport() == SyncServerSupport::UNSUPPORTED) return tr(STR_NOT_SUPPORTED);
  const bool enabled = clippings ? KOREADER_STORE.getSyncClippings() : KOREADER_STORE.getSyncStats();
  return enabled ? nullptr : tr(STR_STATE_OFF);
}
}  // namespace

const char* ReadingSyncUpload::statusLabel(const StatsUploadClient::Result result, const bool clippings) {
  const char* unavailable = unavailableLabel(clippings);
  return unavailable ? unavailable : StatsUploadClient::resultString(result);
}

const char* ReadingSyncUpload::countLabel(char* buffer, const size_t capacity, const uint32_t ok, const uint32_t failed,
                                          const bool clippings) {
  const char* unavailable = unavailableLabel(clippings);
  if (unavailable) return unavailable;
  if (failed == 0)
    snprintf(buffer, capacity, "%u", static_cast<unsigned>(ok));
  else
    snprintf(buffer, capacity, "%u (%u %s)", static_cast<unsigned>(ok), static_cast<unsigned>(failed),
             tr(STR_FAILED_LOWER));
  return buffer;
}

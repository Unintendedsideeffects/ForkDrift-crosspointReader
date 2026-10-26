#include "features/epub/CoverThumbBake.h"

#include <Arduino.h>
#include <FeatureFlags.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <string>

#include "components/UITheme.h"
#include "util/CoverArrivalQueue.h"
#include "util/CoverThumbHeapPolicy.h"

#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif

namespace cover_bake {

#if ENABLE_EPUB_SUPPORT
namespace {

std::string failedMarkerPath(const Epub& epub) { return epub.getCachePath() + "/thumb_failed"; }

// Arrivals whose one attempt was skipped for heap. Boot is the next moment with
// the heap whole; without this a book that is never opened never gets a cover.
constexpr char kPendingPath[] = "/.crosspoint/pending_covers.txt";

std::vector<std::string> readPending() {
  std::vector<std::string> paths;
  HalFile file;
  if (!Storage.openFileForRead("THUMB", kPendingPath, file)) {
    return paths;
  }
  std::string line;
  int c;
  while ((c = file.read()) >= 0) {
    if (c == '\n') {
      if (!line.empty()) paths.push_back(std::move(line));
      line.clear();
    } else {
      line.push_back(static_cast<char>(c));
    }
  }
  if (!line.empty()) paths.push_back(std::move(line));
  return paths;
}

void writePending(const std::vector<std::string>& paths) {
  if (paths.empty()) {
    if (Storage.exists(kPendingPath)) Storage.remove(kPendingPath);
    return;
  }
  HalFile file;
  if (!Storage.openFileForWrite("THUMB", kPendingPath, file)) {
    return;
  }
  for (const auto& path : paths) {
    file.write(reinterpret_cast<const uint8_t*>(path.data()), path.size());
    file.write(static_cast<uint8_t>('\n'));
  }
}

void rememberPending(const std::string& path) {
  std::vector<std::string> paths = readPending();
  if (std::find(paths.begin(), paths.end(), path) != paths.end()) {
    return;
  }
  constexpr size_t kMaxPending = 16;
  if (paths.size() >= kMaxPending) {
    paths.erase(paths.begin());
  }
  paths.push_back(path);
  writePending(paths);
}

enum class Outcome { Present, Baked, Skipped, Failed };

// Outside a reader nothing holds the inflate window, so reserve it first (one
// 32 KB run), then apply the same gate as the reader's inline bake. Releases the
// window again unless someone already held it.
//
// The failure marker is written BEFORE load/convert and cleared after: if a
// malformed EPUB aborts (OOM under -fno-exceptions) or trips the watchdog, the
// marker survives the reset and boot does not retry that book on every power-on.
Outcome bakeOutsideReader(const std::string& path, const char* why) {
  if (!Storage.exists(path.c_str())) {
    return Outcome::Skipped;  // deleted or moved since it was listed
  }
  Epub epub(path, "/.crosspoint");
  if (hasAllThumbs(epub)) {
    return Outcome::Present;
  }
  if (Storage.exists(failedMarkerPath(epub).c_str())) {
    return Outcome::Failed;
  }

  const bool hadWindow = InflateReader::hasSharedWindow();
  if (!hadWindow && !InflateReader::ensureSharedWindow()) {
    LOG_ERR("THUMB", "%s cover skipped (no 32 KB run for the inflate window): %s", why, path.c_str());
    return Outcome::Skipped;
  }
  const CoverThumbMemory snapshot{ESP.getFreeHeap(), ESP.getMaxAllocHeap(), InflateReader::hasSharedWindow()};
  Outcome outcome = Outcome::Skipped;
  if (!CoverThumbHeapPolicy::canBakeInline(snapshot)) {
    LOG_ERR("THUMB", "%s cover skipped: free=%u largest=%u %s", why, snapshot.freeHeap, snapshot.largestBlock,
            path.c_str());
  } else {
    markFailed(epub, true);  // crash guard, see above
    bool ok = false;
    if (epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true)) {
      coverthumbs::Size sizes[8] = {};
      const int count = thumbSizes(sizes, 8);
      Epub::ThumbSize epubSizes[8] = {};
      for (int i = 0; i < count; ++i) {
        epubSizes[i] = Epub::ThumbSize{sizes[i].width, sizes[i].height};
      }
      ok = epub.generateThumbBmps(epubSizes, count);
    }
    markFailed(epub, !ok);
    outcome = ok ? Outcome::Baked : Outcome::Failed;
    if (ok) {
      LOG_INF("THUMB", "%s cover baked: %s", why, path.c_str());
    } else {
      LOG_ERR("THUMB", "%s cover failed, retry on first-page open: %s", why, path.c_str());
    }
  }
  if (!hadWindow) {
    InflateReader::releaseSharedWindow();
  }
  return outcome;
}

}  // namespace
#endif

int thumbSizes(coverthumbs::Size* out, const int maxOut) {
  int count = coverthumbs::all(out, maxOut);
  coverthumbs::appendUnique(out, maxOut, count, {0, UITheme::getInstance().getMetrics().homeCoverHeight});
  return count;
}

bool hasAllThumbs(const Epub& epub) {
#if ENABLE_EPUB_SUPPORT
  coverthumbs::Size sizes[8] = {};
  const int count = thumbSizes(sizes, 8);
  for (int i = 0; i < count; ++i) {
    const std::string thumbPath = sizes[i].width > 0 ? epub.getThumbBmpPath(sizes[i].width, sizes[i].height)
                                                     : epub.getThumbBmpPath(sizes[i].height);
    if (!Storage.exists(thumbPath.c_str())) {
      return false;
    }
  }
  return true;
#else
  (void)epub;
  return true;
#endif
}
void markFailed(const Epub& epub, const bool failed) {
#if ENABLE_EPUB_SUPPORT
  const std::string marker = failedMarkerPath(epub);
  if (failed) {
    HalFile file;
    Storage.openFileForWrite("THUMB", marker, file);
  } else if (Storage.exists(marker.c_str())) {
    Storage.remove(marker.c_str());
  }
#else
  (void)epub;
  (void)failed;
#endif
}

void drainArrival() {
  std::string path;
  if (!CoverArrivalQueue::instance().pop(path)) {
    return;
  }
#if ENABLE_EPUB_SUPPORT
  if (bakeOutsideReader(path, "Arrival") == Outcome::Skipped && Storage.exists(path.c_str())) {
    rememberPending(path);
  }
#endif
}

void catchUpAtBoot(const std::vector<RecentBook>& recentBooks) {
#if ENABLE_EPUB_SUPPORT
  // Boot is the only moment outside a reader with the heap whole: no radio, no Home
  // carousel. Each book costs a few seconds once; after that hasAllThumbs() is a
  // handful of exists() calls.
  std::vector<std::string> stillPending;
  for (const auto& path : readPending()) {
    esp_task_wdt_reset();
    if (bakeOutsideReader(path, "Boot catch-up") == Outcome::Skipped && Storage.exists(path.c_str())) {
      stillPending.push_back(path);
    }
  }
  writePending(stillPending);
  for (const auto& book : recentBooks) {
    if (!FsHelpers::hasEpubExtension(book.path)) {
      continue;
    }
    esp_task_wdt_reset();
    bakeOutsideReader(book.path, "Boot catch-up");
  }
#else
  (void)recentBooks;
#endif
}

}  // namespace cover_bake

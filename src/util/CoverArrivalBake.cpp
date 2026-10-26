#include "util/CoverArrivalBake.h"

#include <Arduino.h>
#include <FeatureFlags.h>
#include <HalStorage.h>
#include <InflateReader.h>
#include <Logging.h>

#include <string>

#include "util/CoverArrivalQueue.h"
#include "util/CoverThumbHeapPolicy.h"
#include "util/CoverThumbSizes.h"

#if ENABLE_EPUB_SUPPORT
#include <Epub.h>
#endif

namespace cover_arrival {

void drainOne() {
  std::string path;
  if (!CoverArrivalQueue::instance().pop(path)) {
    return;
  }
#if ENABLE_EPUB_SUPPORT
  if (!Storage.exists(path.c_str())) {
    return;  // deleted or moved again since it arrived
  }

  // Outside the reader the window is not reserved yet, so this needs room for the
  // window AND the JPEG working set. With the background server resident that is
  // usually not there; the book then gets its thumbs on its first-page open.
  const CoverThumbMemory snapshot{ESP.getFreeHeap(), ESP.getMaxAllocHeap(), InflateReader::hasSharedWindow()};
  if (!CoverThumbHeapPolicy::canAttempt(snapshot)) {
    LOG_ERR("THUMB", "Arrival cover skipped, retry on first-page open: free=%u largest=%u %s", snapshot.freeHeap,
            snapshot.largestBlock, path.c_str());
    return;
  }

  bool success = false;
  {
    Epub epub(path, "/.crosspoint");
    if (epub.load(/*buildIfMissing=*/true, /*skipLoadingCss=*/true)) {
      coverthumbs::Size sizes[8] = {};
      const int count = coverthumbs::all(sizes, 8);
      Epub::ThumbSize epubSizes[8] = {};
      for (int i = 0; i < count; ++i) {
        epubSizes[i] = Epub::ThumbSize{sizes[i].width, sizes[i].height};
      }
      success = epub.generateThumbBmps(epubSizes, count);
    }
  }
  // load() pinned the shared 32 KB window; nothing outside a reader may keep it.
  InflateReader::releaseSharedWindow();
  if (success) {
    LOG_INF("THUMB", "Arrival cover baked: %s", path.c_str());
  } else {
    LOG_ERR("THUMB", "Arrival cover failed, retry on first-page open: %s", path.c_str());
  }
#endif
}

}  // namespace cover_arrival

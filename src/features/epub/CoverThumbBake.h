#pragma once

#include <string>
#include <vector>

#include "util/CoverThumbSizes.h"
#include "util/RecentBooksStore.h"

class Epub;

// Cover thumbnails are made at three moments, never on Home:
//   1. arrival  -- a new book gets one attempt (CoverArrivalQueue, drained by the main loop);
//                  a heap skip is remembered and handed to boot
//   2. boot     -- recent books and remembered arrivals still missing thumbs, before
//                  Wi-Fi and Home take the heap
//   3. reading  -- opening a book on its first page (EpubReaderActivity)
// A book whose cover failed to convert is marked; arrival and boot then leave it to (3).
namespace cover_bake {

// Every size anything draws or serves: the static registry plus the active theme's
// Home cover height (Lyra 226, Flow 370, ...), which the registry omits.
int thumbSizes(coverthumbs::Size* out, int maxOut);

// True when every size from thumbSizes() exists for this book.
bool hasAllThumbs(const Epub& epub);

// The failure marker: set when a real conversion fails, cleared on success.
void markFailed(const Epub& epub, bool failed);

// Main loop only. Pops one book from CoverArrivalQueue and makes its single attempt.
void drainArrival();

// Boot only, before routing: recent EPUBs and remembered arrivals with no thumbs.
void catchUpAtBoot(const std::vector<RecentBook>& recentBooks);

}  // namespace cover_bake

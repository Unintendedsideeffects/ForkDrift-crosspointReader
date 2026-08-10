#pragma once

#include <cstdint>

namespace features::terminus_sleep {

void registerFeature();

// Start the background fetch task and block until it completes or capMs elapses.
// Returns true if the fetch succeeded and the image was pinned.
// Safe to call outside the web handler context (e.g. from timed sleep refresh).
bool startTrmnlFetchAndWait(uint32_t capMs);

// True when the selected Terminus sleep screen has no usable cached image or
// its server-provided refresh interval has elapsed.
bool shouldRefreshBeforeSleep();

}  // namespace features::terminus_sleep

#pragma once

#include <cstdint>
#include <string>

namespace features::terminus_sleep {

void registerFeature();

// Start the background fetch task and block until it completes or capMs elapses.
// Returns true if the fetch succeeded and the image was pinned.
// Safe to call outside the web handler context (e.g. from timed sleep refresh).
bool startTrmnlFetchAndWait(uint32_t capMs);

// True when the selected Terminus sleep screen has no usable cached image or
// its server-provided refresh interval has elapsed.
bool shouldRefreshBeforeSleep();

// Latest validated refresh cadence advertised by /api/display. Defaults to
// the standard Terminus cadence until the first successful manifest fetch.
uint32_t currentRefreshIntervalSeconds();

// Non-secret machine status, including durable timer-refresh evidence. Used by
// both the web status route and the USB device-walk acceptance harness.
std::string machineStatusJson();

}  // namespace features::terminus_sleep

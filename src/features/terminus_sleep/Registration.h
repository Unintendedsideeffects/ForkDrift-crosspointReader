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

// True when the Terminus screen is the sleep screen for this context (reader or
// not) and the device is paired: idle on USB power then shows the live dashboard
// instead of deep-sleeping.
bool chargingDashboardSelected(bool fromReader);

// True while a fetch is running or a failed/deferred one is backing off, i.e.
// starting another now would only fail.
bool fetchBusyOrBackingOff();

// Latest validated refresh cadence advertised by /api/display. Defaults to
// the standard Terminus cadence until the first successful manifest fetch.
uint32_t currentRefreshIntervalSeconds();

// Non-secret machine status, including durable timer-refresh evidence. Used by
// both the web status route and the USB device-walk acceptance harness.
std::string machineStatusJson();

}  // namespace features::terminus_sleep

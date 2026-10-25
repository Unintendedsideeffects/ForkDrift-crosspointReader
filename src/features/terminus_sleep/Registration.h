#pragma once

#include <cstdint>
#include <memory>
#include <string>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace features::terminus_sleep {

void registerFeature();

// Start the background fetch task and block until it completes or capMs elapses.
// Returns true if the fetch succeeded and the image was pinned.
// Safe to call outside the web handler context (e.g. from timed sleep refresh).
bool startTrmnlFetchAndWait(uint32_t capMs);

// True when the selected Terminus sleep screen has no usable cached image or
// its server-provided refresh interval has elapsed.
bool shouldRefreshBeforeSleep();

// The live charging dashboard, when the Terminus screen is the sleep screen for
// this context (reader or not) and the device is paired: idle on USB power then
// shows it instead of deep-sleeping. Null when not selected, compiled out, or on
// OOM. returnBookPath (empty: Home) is where Back and unplug-wake return to.
std::unique_ptr<Activity> makeChargingDashboard(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromReader,
                                                const std::string& returnBookPath);

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

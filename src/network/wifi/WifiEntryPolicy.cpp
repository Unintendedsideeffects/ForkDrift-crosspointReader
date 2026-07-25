#include "network/wifi/WifiEntryPolicy.h"

namespace wifi_entry {

EntryAction evaluateEntry(const EntryInput& input) {
  // Release first, always. The background service owns port 80 and a
  // request-handling task; the caller stops it with keepWifi=true so the radio
  // association survives and can be adopted on the next evaluation.
  if (input.backgroundServiceRunning) {
    return EntryAction::ReleaseBackgroundService;
  }

  // A usable link wins over everything else — no scan, no re-association, no
  // DHCP round trip. Gated on allowAutoConnect: callers that pass
  // autoConnect=false (Settings > WiFi setup, OPDS server setup) opened this
  // screen to *change* networks, so silently adopting would defeat the intent.
  if (input.linkUp && input.allowAutoConnect) {
    return EntryAction::AdoptExistingLink;
  }

  if (!input.linkUp && input.allowAutoConnect && input.hasLastCredential) {
    return EntryAction::AutoConnectLast;
  }

  if (input.scanCacheFresh) {
    return EntryAction::ShowCachedNetworks;
  }

  return EntryAction::Scan;
}

}  // namespace wifi_entry

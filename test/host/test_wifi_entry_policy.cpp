#include "doctest/doctest.h"
#include "network/wifi/WifiEntryPolicy.h"

namespace {

using wifi_entry::EntryAction;
using wifi_entry::EntryInput;

EntryInput makeInput(const bool linkUp = false, const bool backgroundServiceRunning = false,
                     const bool allowAutoConnect = true, const bool hasLastCredential = false,
                     const bool scanCacheFresh = false, const bool backgroundReleaseFailed = false) {
  return EntryInput{
      .linkUp = linkUp,
      .backgroundServiceRunning = backgroundServiceRunning,
      .allowAutoConnect = allowAutoConnect,
      .hasLastCredential = hasLastCredential,
      .scanCacheFresh = scanCacheFresh,
      .backgroundReleaseFailed = backgroundReleaseFailed,
  };
}

}  // namespace

TEST_CASE("wifi entry: background service is released before anything else") {
  // Even with a usable link, the service must be released first — it owns
  // port 80. It stops with keepWifi so the next evaluation adopts the link.
  auto input = makeInput(/*linkUp=*/true, /*backgroundServiceRunning=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::ReleaseBackgroundService);

  input = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/true, /*allowAutoConnect=*/true,
                    /*hasLastCredential=*/true, /*scanCacheFresh=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::ReleaseBackgroundService);
}

TEST_CASE("wifi entry: failed background release falls through to normal entry ladder") {
  const auto adoptInput =
      makeInput(/*linkUp=*/true, /*backgroundServiceRunning=*/true, /*allowAutoConnect=*/true,
                /*hasLastCredential=*/false, /*scanCacheFresh=*/false, /*backgroundReleaseFailed=*/true);
  CHECK(wifi_entry::evaluateEntry(adoptInput) == EntryAction::AdoptExistingLink);

  const auto scanInput =
      makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/true, /*allowAutoConnect=*/false,
                /*hasLastCredential=*/false, /*scanCacheFresh=*/false, /*backgroundReleaseFailed=*/true);
  CHECK(wifi_entry::evaluateEntry(scanInput) == EntryAction::Scan);

  const auto inertInput =
      makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/false,
                /*hasLastCredential=*/false, /*scanCacheFresh=*/false, /*backgroundReleaseFailed=*/true);
  CHECK(wifi_entry::evaluateEntry(inertInput) == EntryAction::Scan);
}

TEST_CASE("wifi entry: an existing link is adopted instead of re-associating") {
  const auto input = makeInput(/*linkUp=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::AdoptExistingLink);
}

TEST_CASE("wifi entry: adoption beats a fresh scan cache and a saved credential") {
  const auto input = makeInput(/*linkUp=*/true, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/true,
                               /*hasLastCredential=*/true, /*scanCacheFresh=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::AdoptExistingLink);
}

TEST_CASE("wifi entry: callers that opted out of auto-connect always get the picker") {
  // Settings > WiFi setup and OPDS server setup pass autoConnect=false because
  // the user opened the screen to change networks. Adopting would defeat that.
  const auto linked = makeInput(/*linkUp=*/true, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/false);
  CHECK(wifi_entry::evaluateEntry(linked) == EntryAction::Scan);

  const auto withCredential = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false,
                                        /*allowAutoConnect=*/false, /*hasLastCredential=*/true);
  CHECK(wifi_entry::evaluateEntry(withCredential) == EntryAction::Scan);

  // ...but a fresh cache is still worth showing instead of a cold scan.
  const auto withCache = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/false,
                                   /*hasLastCredential=*/false, /*scanCacheFresh=*/true);
  CHECK(wifi_entry::evaluateEntry(withCache) == EntryAction::ShowCachedNetworks);
}

TEST_CASE("wifi entry: auto-connect runs when a credential exists and there is no link") {
  const auto input = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/true,
                               /*hasLastCredential=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::AutoConnectLast);
}

TEST_CASE("wifi entry: auto-connect beats a fresh scan cache") {
  // Reconnecting to a known network is faster than making the user pick one.
  const auto input = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/true,
                               /*hasLastCredential=*/true, /*scanCacheFresh=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::AutoConnectLast);
}

TEST_CASE("wifi entry: a fresh cache avoids a cold scan when there is no credential") {
  const auto input = makeInput(/*linkUp=*/false, /*backgroundServiceRunning=*/false, /*allowAutoConnect=*/true,
                               /*hasLastCredential=*/false, /*scanCacheFresh=*/true);
  CHECK(wifi_entry::evaluateEntry(input) == EntryAction::ShowCachedNetworks);
}

TEST_CASE("wifi entry: cold start scans") { CHECK(wifi_entry::evaluateEntry(makeInput()) == EntryAction::Scan); }

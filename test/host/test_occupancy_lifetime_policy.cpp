#include "doctest/doctest.h"
#include "network/background/OccupancyLifetimePolicy.h"

TEST_CASE("Always stays up for Settings rebuild and Home") {
  using namespace occupancy;

  CHECK(evaluateAlwaysStop({}) == AlwaysStop::LeaveRunning);
  CHECK(evaluateAlwaysStop({.rebuildingSettingsLists = true}) == AlwaysStop::LeaveRunning);
}

TEST_CASE("Reader, OPDS, and File Transfer stop Always with keep-wifi") {
  using namespace occupancy;

  CHECK(evaluateAlwaysStop({.occupiesReaderHeap = true}) == AlwaysStop::StopKeepWifi);
  CHECK(evaluateAlwaysStop({.occupiesForegroundHttp = true}) == AlwaysStop::StopKeepWifi);
}

TEST_CASE("reader exit releases the inflate window once no inflate is in flight") {
  using namespace occupancy;

  CHECK(evaluateReaderExitReclaim(false) == ReaderExitReclaim::ReleaseInflateWindow);
  CHECK(evaluateReaderExitReclaim(true) == ReaderExitReclaim::KeepInflateWindow);
}

#include "doctest/doctest.h"
#include "network/background/RecoverHeapAfterWifiPolicy.h"

TEST_CASE("recoverHeapAfterWifi never reboots") {
  using namespace wifi_heap_recovery;

  CHECK(evaluate({true, false}) == Action::Noop);
  CHECK(evaluate({true, true}) == Action::Noop);
  CHECK(evaluate({false, true}) == Action::LeaveWifiForAlways);
  CHECK(evaluate({false, false}) == Action::TearDownWifi);
}

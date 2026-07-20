#include "CrossPointSettings.h"
#include "activities/reader/ReaderInputPolicy.h"
#include "activities/reader/ReaderUtils.h"
#include "doctest/doctest.h"

TEST_CASE("PhysicalConfirmTracker: threshold duration") {
  PhysicalConfirmTracker tracker;

  // Test 599 ms duration
  tracker.update(true, 100);
  tracker.update(false, 699);

  PhysicalConfirmRelease release599 = tracker.peekRelease();
  CHECK(release599.active);
  CHECK(release599.durationMs == 599);

  // Test 600 ms duration
  tracker.update(true, 1000);
  tracker.update(false, 1600);

  PhysicalConfirmRelease release600 = tracker.peekRelease();
  CHECK(release600.active);
  CHECK(release600.durationMs == 600);
}

TEST_CASE("PhysicalConfirmTracker: lifecycle and frame behavior") {
  PhysicalConfirmTracker tracker;

  // Press button
  tracker.update(true, 100);
  CHECK_FALSE(tracker.peekRelease().active);

  // Release button
  tracker.update(false, 700);

  // Release is active for one frame
  PhysicalConfirmRelease release = tracker.peekRelease();
  CHECK(release.active);
  CHECK(release.durationMs == 600);

  // Next update clears release
  tracker.update(false, 800);
  CHECK_FALSE(tracker.peekRelease().active);
}

TEST_CASE("PhysicalConfirmTracker: consume and reset") {
  PhysicalConfirmTracker tracker;

  // Press and release
  tracker.update(true, 100);
  tracker.update(false, 700);

  // Consume hides it in that frame
  tracker.consumeRelease();
  CHECK_FALSE(tracker.peekRelease().active);

  // Next update clears consumed/release
  tracker.update(false, 800);
  CHECK_FALSE(tracker.peekRelease().active);
  CHECK_FALSE(tracker.isConsumed());

  // A later press/release works again
  tracker.update(true, 1000);
  tracker.update(false, 1600);

  PhysicalConfirmRelease releaseLater = tracker.peekRelease();
  CHECK(releaseLater.active);
  CHECK(releaseLater.durationMs == 600);
}

TEST_CASE("ReaderUtils: classifyDualSideConfirmAction") {
  using ReaderUtils::classifyDualSideConfirmAction;
  using ReaderUtils::DualSideConfirmClassification;
  using Action = CrossPointSettings::LONG_PRESS_MENU_ACTION;

  PhysicalConfirmRelease release;
  release.active = true;

  // 599 ms threshold with quick action enabled
  release.durationMs = 599;
  CHECK(classifyDualSideConfirmAction(release, Action::LONG_MENU_TEXT_SELECT) ==
        DualSideConfirmClassification::FALLTHROUGH_TO_PAGE_TURN);

  // 600 ms threshold with quick action enabled
  release.durationMs = 600;
  CHECK(classifyDualSideConfirmAction(release, Action::LONG_MENU_TEXT_SELECT) ==
        DualSideConfirmClassification::DISPATCH_QUICK_ACTION);

  // Action Off always page-turn policy
  release.durationMs = 1000;
  CHECK(classifyDualSideConfirmAction(release, Action::LONG_MENU_OFF) ==
        DualSideConfirmClassification::FALLTHROUGH_TO_PAGE_TURN);

  // Inactive release
  PhysicalConfirmRelease inactiveRelease{false, 1000};
  CHECK(classifyDualSideConfirmAction(inactiveRelease, Action::LONG_MENU_TEXT_SELECT) ==
        DualSideConfirmClassification::IGNORE);
}

TEST_CASE("ReaderInputPolicy: resolveDualSideRightRelease") {
  PhysicalConfirmTracker tracker;

  SUBCASE("Dual-side short Select becomes exactly one forward/page-turn signal") {
    // A short press/release is captured by tracker
    tracker.update(true, 100);
    tracker.update(false, 300); // 200ms
    CHECK(ReaderInputPolicy::resolveDualSideRightRelease(true, false, tracker) == true);
  }

  SUBCASE("Dual-side long Select with a configured action is consumed and does not page-turn") {
    // A long press/release, consumed by quick action
    tracker.update(true, 100);
    tracker.update(false, 800); // 700ms
    tracker.consumeRelease();
    CHECK(ReaderInputPolicy::resolveDualSideRightRelease(true, false, tracker) == false);
  }

  SUBCASE("Dual-side long Select with Action Off falls through to page turn") {
    // A long press/release, but Action Off so not consumed
    tracker.update(true, 100);
    tracker.update(false, 800); // 700ms
    CHECK(ReaderInputPolicy::resolveDualSideRightRelease(true, false, tracker) == true);
  }

  SUBCASE("Physical Right remains a page turn") {
    // No confirm activity
    CHECK(ReaderInputPolicy::resolveDualSideRightRelease(false, true, tracker) == true);
  }

  SUBCASE("The next later release is not suppressed") {
    // If confirm was consumed
    tracker.update(true, 100);
    tracker.update(false, 800);
    tracker.consumeRelease();
    // But then Right is released later in the same frame
    CHECK(ReaderInputPolicy::resolveDualSideRightRelease(true, true, tracker) == true);
  }
}

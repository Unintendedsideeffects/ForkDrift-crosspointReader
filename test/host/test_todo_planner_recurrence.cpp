#include <cstdint>
#include <string>
#include <vector>

#include "activities/todo/TodoItem.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "doctest/doctest.h"
#include "util/DateUtils.h"

// ---------------------------------------------------------------------------
// 1. weekdayIndex
// ---------------------------------------------------------------------------

TEST_CASE("testWeekdayIndex_SaturdayAnchor") {
  // 2026-07-25 is a Saturday → 5
  CHECK(DateUtils::weekdayIndex("2026-07-25") == 5);
}

TEST_CASE("testWeekdayIndex_ThursdayAnchor") {
  // 2026-01-01 is a Thursday → 3
  CHECK(DateUtils::weekdayIndex("2026-01-01") == 3);
}

TEST_CASE("testWeekdayIndex_LeapDay") {
  // 2024-02-29 is a Thursday → 3
  CHECK(DateUtils::weekdayIndex("2024-02-29") == 3);
}

TEST_CASE("testWeekdayIndex_MondayCheck") {
  // 2026-07-20 is a Monday → 0
  CHECK(DateUtils::weekdayIndex("2026-07-20") == 0);
}

TEST_CASE("testWeekdayIndex_MalformedString") {
  CHECK(DateUtils::weekdayIndex("not-a-date") == -1);
  CHECK(DateUtils::weekdayIndex("") == -1);
  CHECK(DateUtils::weekdayIndex("20260725") == -1);  // no dashes
  CHECK(DateUtils::weekdayIndex("26-07-25") == -1);  // too short
}

// ---------------------------------------------------------------------------
// 2. !daily round-trip
// ---------------------------------------------------------------------------

TEST_CASE("testRecurrenceDailyRoundTrip") {
  const std::string line = "- [ ] !daily Morning pages";
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine(line, item));
  CHECK(item.recurrence == TodoRecurrence::Daily);
  CHECK(item.weekdayMask == 0);
  CHECK(item.text == "Morning pages");

  const std::string formatted = TodoPlannerStorage::formatItem(item, true);
  CHECK(formatted == line);
}

// ---------------------------------------------------------------------------
// 3. !wk:mon,wed,fri round-trip
// ---------------------------------------------------------------------------

TEST_CASE("testRecurrenceWeekdaysRoundTrip") {
  const std::string line = "- [ ] !wk:mon,wed,fri Gym";
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine(line, item));
  CHECK(item.recurrence == TodoRecurrence::Weekdays);
  // Mon=bit0, Wed=bit2, Fri=bit4 → 0b0010101 = 0x15 = 21
  CHECK(item.weekdayMask == 0b0010101);
  CHECK(item.text == "Gym");

  const std::string formatted = TodoPlannerStorage::formatItem(item, true);
  CHECK(formatted == line);
}

// ---------------------------------------------------------------------------
// 4. Token order independence
// ---------------------------------------------------------------------------

TEST_CASE("testRecurrenceTokenOrderIndependence") {
  TodoItem itemA{};
  TodoItem itemB{};

  CHECK(TodoPlannerStorage::parseLine("- [ ] !daily !p1 09:00 x", itemA));
  CHECK(TodoPlannerStorage::parseLine("- [ ] !p1 !daily 09:00 x", itemB));

  CHECK(itemA.recurrence == TodoRecurrence::Daily);
  CHECK(itemA.priority == TodoPriority::P1);
  CHECK(itemA.dueMinutes == 9 * 60);
  CHECK(itemA.text == "x");

  CHECK(itemB.recurrence == itemA.recurrence);
  CHECK(itemB.priority == itemA.priority);
  CHECK(itemB.dueMinutes == itemA.dueMinutes);
  CHECK(itemB.text == itemA.text);
}

// ---------------------------------------------------------------------------
// 5. Recurrence token NOT left in item.text
// ---------------------------------------------------------------------------

TEST_CASE("testRecurrenceTokenNotInText") {
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !daily Buy milk", item));
  CHECK(item.text == "Buy milk");
  CHECK(item.recurrence == TodoRecurrence::Daily);

  TodoItem item2{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !wk:tue,thu Run", item2));
  CHECK(item2.text == "Run");
  CHECK(item2.recurrence == TodoRecurrence::Weekdays);
}

// ---------------------------------------------------------------------------
// 6. Malformed tokens stay as literal text
// ---------------------------------------------------------------------------

TEST_CASE("testRecurrenceMalformedTokenStaysLiteral_EmptyWk") {
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !wk: Something", item));
  CHECK(item.recurrence == TodoRecurrence::None);
  CHECK(item.text == "!wk: Something");
}

TEST_CASE("testRecurrenceMalformedTokenStaysLiteral_UnknownDay") {
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !wk:xyz Task", item));
  CHECK(item.recurrence == TodoRecurrence::None);
  CHECK(item.text == "!wk:xyz Task");
}

TEST_CASE("testRecurrenceMalformedTokenStaysLiteral_PartialBadDay") {
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !wk:mon,xyz Task", item));
  CHECK(item.recurrence == TodoRecurrence::None);
  CHECK(item.text == "!wk:mon,xyz Task");
}

// ---------------------------------------------------------------------------
// 7. recursOn
// ---------------------------------------------------------------------------

TEST_CASE("testRecursOn_None") {
  TodoItem item{};
  item.recurrence = TodoRecurrence::None;
  for (int wd = 0; wd <= 6; ++wd) {
    CHECK_FALSE(TodoPlannerStorage::recursOn(item, wd));
  }
}

TEST_CASE("testRecursOn_Daily") {
  TodoItem item{};
  item.recurrence = TodoRecurrence::Daily;
  for (int wd = 0; wd <= 6; ++wd) {
    CHECK(TodoPlannerStorage::recursOn(item, wd));
  }
}

TEST_CASE("testRecursOn_Weekdays") {
  TodoItem item{};
  item.recurrence = TodoRecurrence::Weekdays;
  item.weekdayMask = 0b0010101;                        // Mon, Wed, Fri
  CHECK(TodoPlannerStorage::recursOn(item, 0));        // Mon → true
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 1));  // Tue → false
  CHECK(TodoPlannerStorage::recursOn(item, 2));        // Wed → true
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 3));  // Thu → false
  CHECK(TodoPlannerStorage::recursOn(item, 4));        // Fri → true
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 5));  // Sat → false
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 6));  // Sun → false
}

TEST_CASE("testRecursOn_WeeklyBareMask") {
  TodoItem item{};
  item.recurrence = TodoRecurrence::Weekly;
  item.weekdayMask = 0;
  // Bare !weekly with mask 0 → always true (carries forward unconditionally).
  for (int wd = 0; wd <= 6; ++wd) {
    CHECK(TodoPlannerStorage::recursOn(item, wd));
  }
}

TEST_CASE("testRecursOn_OutOfRangeWeekday") {
  TodoItem item{};
  item.recurrence = TodoRecurrence::Daily;
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, -1));
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 7));
  CHECK_FALSE(TodoPlannerStorage::recursOn(item, 100));
}

// ---------------------------------------------------------------------------
// 8. Rollover selection
// ---------------------------------------------------------------------------

TEST_CASE("testRolloverSelection") {
  // Source: 2026-07-24 (Friday, weekday=4)
  // Target: 2026-07-25 (Saturday, weekday=5)
  const std::string sourceDate = "2026-07-24";
  const std::string targetDate = "2026-07-25";

  std::vector<TodoItem> sourceItems;

  // Item 0: unchecked, daily recurring → should be carried
  TodoItem daily{};
  daily.text = "Daily task";
  daily.recurrence = TodoRecurrence::Daily;
  sourceItems.push_back(daily);

  // Item 1: checked, daily recurring → should NOT be carried (checked)
  TodoItem checkedDaily{};
  checkedDaily.text = "Done daily";
  checkedDaily.checked = true;
  checkedDaily.recurrence = TodoRecurrence::Daily;
  sourceItems.push_back(checkedDaily);

  // Item 2: unchecked, no recurrence → should NOT be carried
  TodoItem plain{};
  plain.text = "One-off task";
  sourceItems.push_back(plain);

  // Item 3: header → should NOT be carried
  TodoItem header{};
  header.text = "Morning";
  header.isHeader = true;
  header.isSection = true;
  sourceItems.push_back(header);

  // Item 4: unchecked, weekdays Mon-Fri → NOT carried (target is Saturday)
  TodoItem weekdayOnly{};
  weekdayOnly.text = "Weekday task";
  weekdayOnly.recurrence = TodoRecurrence::Weekdays;
  weekdayOnly.weekdayMask = 0b0011111;  // Mon-Fri
  sourceItems.push_back(weekdayOnly);

  // Item 5: unchecked, weekdays includes Sat → should be carried
  TodoItem satTask{};
  satTask.text = "Weekend task";
  satTask.recurrence = TodoRecurrence::Weekdays;
  satTask.weekdayMask = 0b1100000;  // Sat + Sun
  sourceItems.push_back(satTask);

  std::vector<TodoItem> definitions;
  TodoPlannerStorage::collectRecurringDefinitions(sourceItems, sourceDate, definitions);
  const auto result = TodoPlannerStorage::selectDueOn(definitions, targetDate);
  REQUIRE(result.size() == 2);
  CHECK(result[0].text == "Daily task");
  CHECK(result[0].recurrence == TodoRecurrence::Daily);
  CHECK_FALSE(result[0].checked);
  CHECK(result[1].text == "Weekend task");
  CHECK(result[1].recurrence == TodoRecurrence::Weekdays);
  CHECK_FALSE(result[1].checked);
}

// ---------------------------------------------------------------------------
// 9. !weekly normalisation
// ---------------------------------------------------------------------------

TEST_CASE("testWeeklyNormalisesToConcreteDay") {
  // Source: 2026-07-24 (Friday, weekday=4)
  const std::string sourceDate = "2026-07-24";

  std::vector<TodoItem> sourceItems;
  TodoItem weeklyItem{};
  weeklyItem.text = "Weekly thing";
  weeklyItem.recurrence = TodoRecurrence::Weekly;
  weeklyItem.weekdayMask = 0;  // bare !weekly
  sourceItems.push_back(weeklyItem);

  std::vector<TodoItem> definitions;
  TodoPlannerStorage::collectRecurringDefinitions(sourceItems, sourceDate, definitions);

  // Normalisation happens when the definition is collected, BEFORE the due-date
  // test — so "weekly" means "this same weekday each week" rather than
  // degenerating into "every day until something normalises it".
  REQUIRE(definitions.size() == 1);
  CHECK(definitions[0].recurrence == TodoRecurrence::Weekdays);
  // Source is Friday (weekday 4) → bit 4 → mask 0b0010000 = 16
  CHECK(definitions[0].weekdayMask == (1u << 4));

  // Formatting the normalised item produces !wk:fri, NOT !weekly.
  CHECK(TodoPlannerStorage::formatItem(definitions[0], true) == "- [ ] !wk:fri Weekly thing");

  // Due the NEXT Friday (2026-07-31), not the intervening Saturday.
  CHECK(TodoPlannerStorage::selectDueOn(definitions, "2026-07-25").empty());
  const auto nextFriday = TodoPlannerStorage::selectDueOn(definitions, "2026-07-31");
  REQUIRE(nextFriday.size() == 1);
  CHECK(nextFriday[0].text == "Weekly thing");
}

TEST_CASE("testWeeklyParseFormat") {
  // !weekly is accepted on input
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !weekly Review", item));
  CHECK(item.recurrence == TodoRecurrence::Weekly);
  CHECK(item.weekdayMask == 0);
  CHECK(item.text == "Review");

  // Formatting a Weekly item produces !weekly (round-trip preserves in-memory data)
  const std::string formatted = TodoPlannerStorage::formatItem(item, true);
  CHECK(formatted == "- [ ] !weekly Review");
}

// ---------------------------------------------------------------------------
// Additional: full format with recurrence + priority + due time
// ---------------------------------------------------------------------------

TEST_CASE("testFormatEmissionOrder") {
  TodoItem item{};
  item.text = "Stand-up";
  item.recurrence = TodoRecurrence::Daily;
  item.priority = TodoPriority::P1;
  item.dueMinutes = 9 * 60 + 30;
  item.checked = false;

  const std::string formatted = TodoPlannerStorage::formatItem(item, true);
  CHECK(formatted == "- [ ] !daily !p1 09:30 Stand-up");
}

TEST_CASE("testRecurrenceCheckedItemFormat") {
  TodoItem item{};
  item.text = "Gym";
  item.recurrence = TodoRecurrence::Weekdays;
  item.weekdayMask = 0b0010101;  // Mon, Wed, Fri
  item.checked = true;

  const std::string formatted = TodoPlannerStorage::formatItem(item, true);
  CHECK(formatted == "- [x] !wk:mon,wed,fri Gym");
}

// ---------------------------------------------------------------------------
// parseRecurrenceToken edge cases
// ---------------------------------------------------------------------------

TEST_CASE("testParseRecurrenceToken_CaseInsensitive") {
  TodoRecurrence rec = TodoRecurrence::None;
  uint8_t mask = 0;
  CHECK(TodoPlannerStorage::parseRecurrenceToken("!DAILY", rec, mask));
  CHECK(rec == TodoRecurrence::Daily);

  CHECK(TodoPlannerStorage::parseRecurrenceToken("!Daily", rec, mask));
  CHECK(rec == TodoRecurrence::Daily);

  CHECK(TodoPlannerStorage::parseRecurrenceToken("!WK:Mon,Fri", rec, mask));
  CHECK(rec == TodoRecurrence::Weekdays);
  CHECK(mask == 0b0010001);  // Mon + Fri
}

TEST_CASE("testParseRecurrenceToken_Unknown") {
  TodoRecurrence rec = TodoRecurrence::None;
  uint8_t mask = 0;
  CHECK_FALSE(TodoPlannerStorage::parseRecurrenceToken("!unknown", rec, mask));
  CHECK_FALSE(TodoPlannerStorage::parseRecurrenceToken("!wk:", rec, mask));
  CHECK_FALSE(TodoPlannerStorage::parseRecurrenceToken("!p1", rec, mask));
}

// ---------------------------------------------------------------------------
// Existing lines with literal ! that are NOT recurrence tokens
// ---------------------------------------------------------------------------

TEST_CASE("testLiteralBangNotMistaken") {
  // An unknown !-prefixed word should stay as literal text.
  TodoItem item{};
  CHECK(TodoPlannerStorage::parseLine("- [ ] !important Read this", item));
  CHECK(item.recurrence == TodoRecurrence::None);
  CHECK(item.text == "!important Read this");
}

// ---------------------------------------------------------------------------
// 10. Definitions survive days they are not due on
// ---------------------------------------------------------------------------

// Regression: an earlier implementation collected only the items DUE on the
// target day, from the single most recent file. A "!wk:mon" definition is
// absent from every non-Monday file, so as soon as an intermediate day gained a
// file of its own the definition was lost forever after one cycle.
TEST_CASE("testRecurrenceSurvivesIntermediateDays") {
  // Monday 2026-07-20 holds the definition; Tuesday 2026-07-21 has an unrelated
  // one-off task; the target is the following Monday 2026-07-27.
  std::vector<TodoItem> mondayItems;
  TodoItem gym{};
  gym.text = "Gym";
  gym.recurrence = TodoRecurrence::Weekdays;
  gym.weekdayMask = 1u << 0;  // Mon
  mondayItems.push_back(gym);

  std::vector<TodoItem> tuesdayItems;
  TodoItem milk{};
  milk.text = "Buy milk";  // non-recurring one-off
  tuesdayItems.push_back(milk);

  // Accumulate most-recent-day-first, as the activity's backwards scan does.
  std::vector<TodoItem> definitions;
  TodoPlannerStorage::collectRecurringDefinitions(tuesdayItems, "2026-07-21", definitions);
  TodoPlannerStorage::collectRecurringDefinitions(mondayItems, "2026-07-20", definitions);

  // The one-off contributes nothing; the Monday definition survives Tuesday.
  REQUIRE(definitions.size() == 1);
  CHECK(definitions[0].text == "Gym");

  const auto dueNextMonday = TodoPlannerStorage::selectDueOn(definitions, "2026-07-27");
  REQUIRE(dueNextMonday.size() == 1);
  CHECK(dueNextMonday[0].text == "Gym");

  // ...and is correctly absent on a day it does not recur on.
  CHECK(TodoPlannerStorage::selectDueOn(definitions, "2026-07-28").empty());
}

TEST_CASE("testRecurringDefinitionDedupeKeepsNewest") {
  // Same text on two days; the most recently seen (appended first) wins.
  std::vector<TodoItem> newer;
  TodoItem a{};
  a.text = "Stretch";
  a.recurrence = TodoRecurrence::Daily;
  newer.push_back(a);

  std::vector<TodoItem> older;
  TodoItem b{};
  b.text = "Stretch";
  b.recurrence = TodoRecurrence::Weekdays;
  b.weekdayMask = 1u << 2;  // Wed
  older.push_back(b);

  std::vector<TodoItem> definitions;
  TodoPlannerStorage::collectRecurringDefinitions(newer, "2026-07-24", definitions);
  TodoPlannerStorage::collectRecurringDefinitions(older, "2026-07-20", definitions);

  REQUIRE(definitions.size() == 1);
  CHECK(definitions[0].recurrence == TodoRecurrence::Daily);
}

// Checked recurring items are not carried; the day's completion does not repeat.
TEST_CASE("testCheckedRecurringItemNotCarried") {
  std::vector<TodoItem> sourceItems;
  TodoItem done{};
  done.text = "Meditate";
  done.recurrence = TodoRecurrence::Daily;
  done.checked = true;
  sourceItems.push_back(done);

  std::vector<TodoItem> definitions;
  TodoPlannerStorage::collectRecurringDefinitions(sourceItems, "2026-07-24", definitions);
  CHECK(definitions.empty());
}

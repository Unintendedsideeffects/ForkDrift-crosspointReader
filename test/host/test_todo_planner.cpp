#include <string>

#include "doctest/doctest.h"
#include "src/activities/todo/TodoPlannerStorage.h"

TEST_CASE("testTodoPlannerStorageSelection") {
  const std::string isoDate = "2026-02-17";
  const std::string alternateDate = "17.02.2026";
  CHECK(TodoPlannerStorage::dailyPath(isoDate, true, true, false) == "/daily/2026-02-17.md");
  CHECK(TodoPlannerStorage::dailyPath(isoDate, false, false, true) == "/daily/2026-02-17.txt");
  CHECK(TodoPlannerStorage::dailyPath(isoDate, false, true, false) == "/daily/2026-02-17.md");
  CHECK(TodoPlannerStorage::dailyPath(isoDate, true, false, false) == "/daily/2026-02-17.md");
  CHECK(TodoPlannerStorage::dailyPath(isoDate, false, false, false) == "/daily/2026-02-17.txt");
  CHECK(TodoPlannerStorage::dailyPath(alternateDate, false, false, false) == "/daily/17.02.2026.txt");
  CHECK(TodoPlannerStorage::formatEntry("Task", false) == "- [ ] Task");
  CHECK(TodoPlannerStorage::formatEntry("Agenda item", true) == "Agenda item");
  CHECK(TodoPlannerStorage::kTodoEntryMaxTextLength == 300);
}

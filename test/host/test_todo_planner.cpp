#include <string>
#include <vector>

#include "activities/todo/TodoItem.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "doctest/doctest.h"

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

TEST_CASE("testTodoPlannerStorageParseLine") {
  TodoItem item{};

  CHECK(TodoPlannerStorage::parseLine("- [ ] Buy milk", item));
  CHECK_FALSE(item.checked);
  CHECK_FALSE(item.isHeader);
  CHECK(item.text == "Buy milk");

  CHECK(TodoPlannerStorage::parseLine("- [x] Done task", item));
  CHECK(item.checked);
  CHECK(item.text == "Done task");

  CHECK(TodoPlannerStorage::parseLine("> Meeting notes", item));
  CHECK(item.isHeader);
  CHECK(item.isAgenda);
  CHECK(item.text == "Meeting notes");

  CHECK(TodoPlannerStorage::parseLine("## Morning", item));
  CHECK(item.isHeader);
  CHECK(item.isSection);
  CHECK(item.text == "Morning");

  CHECK(TodoPlannerStorage::parseLine("- [ ] !p1 Call client", item));
  CHECK(item.priority == TodoPriority::P1);
  CHECK(item.text == "Call client");

  CHECK(TodoPlannerStorage::parseLine("- [ ] 14:30 Standup", item));
  CHECK(item.dueMinutes == 14 * 60 + 30);
  CHECK(item.text == "Standup");

  CHECK(TodoPlannerStorage::parseLine("- [ ] !p2 09:15 Review !p2 ignored", item));
  CHECK(item.priority == TodoPriority::P2);
  CHECK(item.dueMinutes == 9 * 60 + 15);
  CHECK(item.text == "Review !p2 ignored");
}

TEST_CASE("testTodoPlannerStorageRoundTrip") {
  const std::vector<TodoItem> items = {
      {.text = "Agenda", .checked = false, .isHeader = true, .isAgenda = true},
      {.text = "Morning", .checked = false, .isHeader = true, .isSection = true},
      {.text = "Plain note", .checked = false, .isHeader = true},
      {.text = "Task one", .checked = false, .isHeader = false, .priority = TodoPriority::P1, .dueMinutes = 870},
      {.text = "Task two", .checked = true, .isHeader = false},
  };

  const std::string markdown = TodoPlannerStorage::formatFile(items, true);
  CHECK(markdown == "> Agenda\n## Morning\nPlain note\n- [ ] !p1 14:30 Task one\n- [x] Task two\n");

  std::vector<TodoItem> parsed;
  TodoPlannerStorage::parseFile(markdown, parsed);
  REQUIRE(parsed.size() == items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    CHECK(parsed[i].text == items[i].text);
    CHECK(parsed[i].checked == items[i].checked);
    CHECK(parsed[i].isHeader == items[i].isHeader);
    CHECK(parsed[i].isAgenda == items[i].isAgenda);
    CHECK(parsed[i].isSection == items[i].isSection);
    CHECK(parsed[i].priority == items[i].priority);
    CHECK(parsed[i].dueMinutes == items[i].dueMinutes);
  }
}

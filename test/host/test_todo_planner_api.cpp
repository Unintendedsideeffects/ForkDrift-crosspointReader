#include "doctest/doctest.h"
#include "network/TodoPlannerApi.h"
#include "test/mock/HalStorage.h"

TEST_CASE("testTodoEntryRequestValidation") {
  Storage.reset();

  auto disabled = network::handleTodoEntryRequest(false, true, "Task", "", "2026-04-11");
  CHECK(disabled.statusCode == 404);

  auto missingDate = network::handleTodoEntryRequest(true, true, "Task", "", "");
  CHECK(missingDate.statusCode == 503);

  auto invalidText = network::handleTodoEntryRequest(true, true, "\n\t  ", "", "2026-04-11");
  CHECK(invalidText.statusCode == 400);

  auto ok = network::handleTodoEntryRequest(true, true, "Task", "", "2026-04-11");
  CHECK(ok.statusCode == 200);
  CHECK(ok.targetPath == "/daily/2026-04-11.md");
  CHECK(Storage.readFile("/daily/2026-04-11.md") == "- [ ] Task\n");
}

TEST_CASE("testTodoTodayGetAndSaveRequests") {
  Storage.reset();

  auto missingBody = network::handleTodoTodaySaveRequest(true, true, false, "", "2026-04-11");
  CHECK(missingBody.statusCode == 400);

  auto invalidJson = network::handleTodoTodaySaveRequest(true, true, true, "{", "2026-04-11");
  CHECK(invalidJson.statusCode == 400);

  auto missingItems = network::handleTodoTodaySaveRequest(true, true, true, "{}", "2026-04-11");
  CHECK(missingItems.statusCode == 400);

  auto save = network::handleTodoTodaySaveRequest(
      true, true, true,
      "{\"items\":[{\"text\":\"Task one\",\"type\":\"todo\",\"checked\":false,\"isHeader\":false},"
      "{\"text\":\"Agenda\",\"type\":\"agenda\",\"checked\":false,\"isHeader\":true}]}",
      "2026-04-11");
  CHECK(save.statusCode == 200);
  CHECK(save.targetPath == "/daily/2026-04-11.md");
  CHECK(Storage.readFile("/daily/2026-04-11.md") == "- [ ] Task one\n> Agenda\n");

  auto get = network::handleTodoTodayGetRequest(true, true, "2026-04-11");
  CHECK(get.statusCode == 200);
  CHECK(get.body.indexOf("\"date\":\"2026-04-11\"") != -1);
  CHECK(get.body.indexOf("\"path\":\"/daily/2026-04-11.md\"") != -1);
  CHECK(get.body.indexOf("\"text\":\"Task one\"") != -1);
  CHECK(get.body.indexOf("\"type\":\"agenda\"") != -1);
}

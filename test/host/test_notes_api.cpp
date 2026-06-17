#include "HalStorage.h"
#include "doctest/doctest.h"
#include "network/server/NotesApi.h"

TEST_CASE("testNotesEntryRequestValidation") {
  Storage.remove("/notes.txt");

  auto disabled = network::handleNotesEntryRequest(false, "Note");
  CHECK(disabled.statusCode == 404);

  auto invalid = network::handleNotesEntryRequest(true, "\n\t ");
  CHECK(invalid.statusCode == 400);

  auto ok = network::handleNotesEntryRequest(true, "First note");
  CHECK(ok.statusCode == 200);

  auto get = network::handleNotesGetRequest(true);
  CHECK(get.statusCode == 200);
  CHECK(get.body.indexOf("First note") != -1);
}

TEST_CASE("testNotesSaveAcceptsStringsAndObjects") {
  Storage.remove("/notes.txt");

  auto missingBody = network::handleNotesSaveRequest(true, false, "");
  CHECK(missingBody.statusCode == 400);

  auto invalidJson = network::handleNotesSaveRequest(true, true, "{");
  CHECK(invalidJson.statusCode == 400);

  auto missingItems = network::handleNotesSaveRequest(true, true, "{}");
  CHECK(missingItems.statusCode == 400);

  auto save = network::handleNotesSaveRequest(true, true, "{\"items\":[\"One\",{\"text\":\"Two\"}]}");
  CHECK(save.statusCode == 200);

  auto get = network::handleNotesGetRequest(true);
  CHECK(get.statusCode == 200);
  CHECK(get.body.indexOf("One") != -1);
  CHECK(get.body.indexOf("Two") != -1);
  CHECK(Storage.readFile("/notes.txt") == "One\nTwo\n");
}

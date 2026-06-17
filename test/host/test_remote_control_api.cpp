#include "doctest/doctest.h"
#include "network/server/RemoteControlApi.h"
#include "test/mock/HalStorage.h"

TEST_CASE("testOpenBookHttpRequestValidation") {
  Storage.reset();

  auto missingBody = network::parseOpenBookHttpRequest(false, "");
  CHECK(missingBody.statusCode == 400);
  CHECK(std::string(missingBody.body) == "Missing JSON body");

  auto invalidJson = network::parseOpenBookHttpRequest(true, "{");
  CHECK(invalidJson.statusCode == 400);
  CHECK(std::string(invalidJson.body) == "Invalid JSON");

  auto missingPath = network::parseOpenBookHttpRequest(true, "{}");
  CHECK(missingPath.statusCode == 400);
  CHECK(std::string(missingPath.body) == "Missing path");

  auto invalidPath = network::parseOpenBookHttpRequest(true, "{\"path\":\"../books/demo.epub\"}");
  CHECK(invalidPath.statusCode == 400);
  CHECK(std::string(invalidPath.body) == "Invalid path");

  auto missingFile = network::parseOpenBookHttpRequest(true, "{\"path\":\"/books/demo.epub\"}");
  CHECK(missingFile.statusCode == 404);
  CHECK(std::string(missingFile.body) == "File not found");

  CHECK(Storage.writeFile("/books/demo.epub", "demo"));
  auto accepted = network::parseOpenBookHttpRequest(true, "{\"path\":\"/books/demo.epub\"}");
  CHECK(accepted.statusCode == 202);
  CHECK(std::string(accepted.contentType) == "application/json");
  CHECK(std::string(accepted.body) == "{\"status\":\"opening\"}");
  CHECK(accepted.path == "/books/demo.epub");
}

TEST_CASE("testRemoteButtonHttpRequestValidation") {
  auto missingBody = network::parseRemoteButtonHttpRequest(false, "");
  CHECK(missingBody.statusCode == 400);
  CHECK(std::string(missingBody.body) == "Missing JSON body");

  auto invalidJson = network::parseRemoteButtonHttpRequest(true, "{");
  CHECK(invalidJson.statusCode == 400);
  CHECK(std::string(invalidJson.body) == "Invalid JSON");

  auto invalidButton = network::parseRemoteButtonHttpRequest(true, "{\"button\":\"home\"}");
  CHECK(invalidButton.statusCode == 400);
  CHECK(std::string(invalidButton.body) == "Unknown button; use page_forward or page_back");

  auto forward = network::parseRemoteButtonHttpRequest(true, "{\"button\":\"page_forward\"}");
  CHECK(forward.statusCode == 202);
  CHECK(forward.pageTurn == 1);
  CHECK(std::string(forward.body) == "{\"status\":\"ok\"}");

  auto next = network::parseRemoteButtonHttpRequest(true, "{\"button\":\"next\"}");
  CHECK(next.statusCode == 202);
  CHECK(next.pageTurn == 1);

  auto previous = network::parseRemoteButtonHttpRequest(true, "{\"button\":\"previous\"}");
  CHECK(previous.statusCode == 202);
  CHECK(previous.pageTurn == -1);
}

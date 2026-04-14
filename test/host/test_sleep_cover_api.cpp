#include "doctest/doctest.h"

#include "network/SleepCoverApi.h"
#include "test/mock/HalStorage.h"

#include <cstring>
#include <string>

namespace {

bool saveSettingsOk() { return true; }

bool saveSettingsFail() { return false; }

}  // namespace

TEST_CASE("sleep cover get returns pinned path metadata") {
  const auto result = network::buildSleepCoverGetResponse("/sleep/.pinned-cover.bmp");
  CHECK(result.statusCode == 200);
  CHECK(result.body.indexOf("\"path\":\"/sleep/.pinned-cover.bmp\"") != -1);
  CHECK(result.body.indexOf("\"name\":\".pinned-cover.bmp\"") != -1);
}

TEST_CASE("sleep cover pin clears pinned path") {
  char pinnedPath[256];
  std::strcpy(pinnedPath, "/sleep/existing.bmp");

  const auto result = network::handleSleepCoverPinRequest(true, "{\"path\":\"\"}", pinnedPath, sizeof(pinnedPath),
                                                          nullptr, saveSettingsOk);

  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(result.contentType, "text/plain") == 0);
  CHECK(result.body == "Cleared");
  CHECK(std::strcmp(pinnedPath, "") == 0);
}

TEST_CASE("sleep cover pin validates request and path existence") {
  Storage.reset();
  char pinnedPath[256] = "";

  auto missingBody =
      network::handleSleepCoverPinRequest(false, "", pinnedPath, sizeof(pinnedPath), nullptr, saveSettingsOk);
  CHECK(missingBody.statusCode == 400);

  auto invalidJson =
      network::handleSleepCoverPinRequest(true, "{", pinnedPath, sizeof(pinnedPath), nullptr, saveSettingsOk);
  CHECK(invalidJson.statusCode == 400);

  auto invalidPath = network::handleSleepCoverPinRequest(true, "{\"path\":\"../bad\"}", pinnedPath, sizeof(pinnedPath),
                                                         nullptr, saveSettingsOk);
  CHECK(invalidPath.statusCode == 400);

  auto missingFile = network::handleSleepCoverPinRequest(true, "{\"path\":\"/sleep/missing.bmp\"}", pinnedPath,
                                                         sizeof(pinnedPath), nullptr, saveSettingsOk);
  CHECK(missingFile.statusCode == 404);
}

TEST_CASE("sleep cover pin saves direct path") {
  Storage.reset();
  CHECK(Storage.writeFile("/sleep/example.bmp", "bmp"));

  char pinnedPath[256] = "";
  const auto result = network::handleSleepCoverPinRequest(true, "{\"path\":\"/sleep/example.bmp\"}", pinnedPath,
                                                          sizeof(pinnedPath), nullptr, saveSettingsOk);

  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(pinnedPath, "/sleep/example.bmp") == 0);
  CHECK(result.body.indexOf("\"pinnedPath\":\"/sleep/example.bmp\"") != -1);
}

TEST_CASE("sleep cover pin can copy a resolved book cover") {
  Storage.reset();
  CHECK(Storage.writeFile("/covers/book.bmp", "cover-bytes"));

  char pinnedPath[256] = "";
  const auto result = network::handleSleepCoverPinRequest(
      true, "{\"bookPath\":\"/books/demo.epub\"}", pinnedPath, sizeof(pinnedPath),
      [](const String& bookPath, std::string& coverPath) {
        if (bookPath != "/books/demo.epub") {
          return false;
        }
        coverPath = "/covers/book.bmp";
        return true;
      },
      saveSettingsOk);

  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(pinnedPath, "/sleep/.pinned-cover.bmp") == 0);
  CHECK(Storage.readFile("/sleep/.pinned-cover.bmp") == "cover-bytes");
}

TEST_CASE("sleep cover pin surfaces resolver and save failures") {
  Storage.reset();
  char pinnedPath[256] = "";

  auto noCover = network::handleSleepCoverPinRequest(
      true, "{\"bookPath\":\"/books/demo.epub\"}", pinnedPath, sizeof(pinnedPath),
      [](const String&, std::string&) { return false; }, saveSettingsOk);
  CHECK(noCover.statusCode == 404);

  CHECK(Storage.writeFile("/sleep/example.bmp", "bmp"));
  auto saveFailed = network::handleSleepCoverPinRequest(true, "{\"path\":\"/sleep/example.bmp\"}", pinnedPath,
                                                        sizeof(pinnedPath), nullptr, saveSettingsFail);
  CHECK(saveFailed.statusCode == 500);
}

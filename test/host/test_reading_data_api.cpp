#include "doctest/doctest.h"

#include <ArduinoJson.h>

#include "lib/Serialization/Serialization.h"
#include "network/ReadingDataApi.h"
#include "test/mock/HalStorage.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

std::string buildCachePath(const char* prefix, const std::string& bookPath) {
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(bookPath));
}

void writeTxtIndexFile(const std::string& path, const uint32_t totalPages, const uint8_t markdownFlag = 0) {
  FsFile f;
  CHECK(Storage.openFileForWrite("TST", path, f));
  serialization::writePod(f, static_cast<uint32_t>(0x54585449));
  serialization::writePod(f, static_cast<uint8_t>(3));
  serialization::writePod(f, static_cast<uint32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(480));
  serialization::writePod(f, static_cast<int32_t>(20));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<int32_t>(0));
  serialization::writePod(f, static_cast<uint8_t>(0));
  serialization::writePod(f, markdownFlag);
  serialization::writePod(f, totalPages);
  f.close();
}

}  // namespace

TEST_CASE("reading data api builds recent books response") {
  Storage.reset();

  const std::string bookPath = "/books/recent-api.txt";
  const std::string cachePath = buildCachePath("txt_", bookPath);
  CHECK(Storage.writeFile(bookPath.c_str(), "demo"));

  FsFile progressFile;
  CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
  const uint8_t progressBytes[4] = {9, 0, 0, 0};
  progressFile.write(progressBytes, sizeof(progressBytes));
  progressFile.close();
  writeTxtIndexFile(cachePath + "/index.bin", 40);

  const std::vector<RecentBook> books = {
      {bookPath, "Recent API Demo", "Unit Tester", "/covers/recent.bmp"},
  };

  const auto result = network::buildRecentBooksResponse(books, false);
  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(result.contentType, "application/json") == 0);
  CHECK(result.body.indexOf("\"title\":\"Recent API Demo\"") != -1);
  CHECK(result.body.indexOf("\"last_position\":\"10/40 25%\"") != -1);
}

TEST_CASE("reading data api validates book progress requests") {
  Storage.reset();

  CHECK(network::handleBookProgressRequest("").statusCode == 400);
  CHECK(network::handleBookProgressRequest("../bad").statusCode == 400);
  CHECK(network::handleBookProgressRequest("/.crosspoint/secret.epub").statusCode == 403);
  CHECK(network::handleBookProgressRequest("/books/missing.epub").statusCode == 404);

  CHECK(Storage.writeFile("/books/demo.pdf", "pdf"));
  CHECK(network::handleBookProgressRequest("/books/demo.pdf").statusCode == 400);
}

TEST_CASE("reading data api returns progress payload") {
  Storage.reset();

  const std::string bookPath = "/books/progress-api.txt";
  const std::string cachePath = buildCachePath("txt_", bookPath);
  CHECK(Storage.writeFile(bookPath.c_str(), "demo"));

  FsFile progressFile;
  CHECK(Storage.openFileForWrite("TST", cachePath + "/progress.bin", progressFile));
  const uint8_t progressBytes[4] = {4, 0, 0, 0};
  progressFile.write(progressBytes, sizeof(progressBytes));
  progressFile.close();
  writeTxtIndexFile(cachePath + "/index.bin", 20);

  const auto result = network::handleBookProgressRequest(bookPath.c_str());
  CHECK(result.statusCode == 200);
  CHECK(std::strcmp(result.contentType, "application/json") == 0);

  JsonDocument doc;
  CHECK(!deserializeJson(doc, result.body.c_str()));
  CHECK(std::string(doc["path"] | "") == bookPath);
  CHECK(std::string(doc["progress"]["format"] | "") == "txt");
  CHECK(doc["progress"]["page"] == 5);
  CHECK(doc["progress"]["pageCount"] == 20);
  CHECK(std::fabs(static_cast<float>(doc["progress"]["percent"] | 0.0f) - 25.0f) < 0.01f);
}

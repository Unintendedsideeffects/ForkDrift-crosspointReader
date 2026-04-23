#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"

namespace {

std::string readTextFile(const char* path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    const std::string message = std::string("Failed to open ") + path;
    FAIL_CHECK(message.c_str());
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

unsigned long extractFirstUnsigned(const std::string& content, const std::regex& pattern, const char* description) {
  std::smatch match;
  if (!std::regex_search(content, match, pattern)) {
    const std::string message = std::string("Missing pattern for ") + description;
    FAIL_CHECK(message.c_str());
    return 0;
  }
  REQUIRE(match.size() >= 2);
  return std::stoul(match[1].str());
}

struct StackBudgetExpectation {
  const char* path;
  const char* description;
  std::regex pattern;
  unsigned long minimumBytes;
};

struct ByteArrayBudget {
  const char* path;
  unsigned long maxBytes;
};

}  // namespace

TEST_CASE("network task stack budgets stay above safe minimums") {
  const StackBudgetExpectation expectations[] = {
      {"src/network/BackgroundWifiService.h", "background WiFi task stack", std::regex(R"(TASK_STACK\s*=\s*(\d+))"),
       4096},
      {"src/network/BackgroundWebServer.cpp", "background web server NTP task stack",
       std::regex(R"(xTaskCreate\(\s*ntpSyncTask\s*,\s*"TimeSyncTask"\s*,\s*(\d+))"), 4096},
      {"src/network/RemoteKeyboardManager.cpp", "remote keyboard hotspot task stack",
       std::regex(R"(xTaskCreate\(\s*&RemoteKeyboardManager::hotspotTaskEntry\s*,\s*"kbhotspot"\s*,\s*(\d+))"), 6144},
  };

  for (const StackBudgetExpectation& expectation : expectations) {
    const std::string content = readTextFile(expectation.path);
    const unsigned long actual = extractFirstUnsigned(content, expectation.pattern, expectation.description);
    CHECK_MESSAGE(actual >= expectation.minimumBytes,
                  expectation.description << " regressed to " << actual << " bytes");
  }
}

TEST_CASE("fileserver startup code avoids oversized local byte buffers") {
  const ByteArrayBudget budgets[] = {
      {"src/network/BackgroundWifiService.cpp", 256},
      {"src/network/BackgroundWebServer.cpp", 256},
      {"src/activities/network/CrossPointWebServerActivity.cpp", 256},
      {"src/network/CrossPointWebServer.cpp", 1024},
  };

  const std::regex byteArrayPattern(R"(\b(?:char|uint8_t)\s+\w+\[(\d+)\])");

  for (const ByteArrayBudget& budget : budgets) {
    const std::string content = readTextFile(budget.path);
    std::istringstream lines(content);
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(lines, line)) {
      lineNumber++;

      std::smatch match;
      if (!std::regex_search(line, match, byteArrayPattern)) {
        continue;
      }

      const unsigned long bytes = std::stoul(match[1].str());
      CHECK_MESSAGE(bytes <= budget.maxBytes, budget.path << ":" << lineNumber << " uses " << bytes
                                                          << " stack bytes; max allowed is " << budget.maxBytes);
    }
  }
}

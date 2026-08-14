#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "core/registries/ReaderRegistry.h"
#include "doctest/doctest.h"

namespace {

std::string readFile(const std::string& path) {
  std::ifstream ifs(path);
  REQUIRE(ifs.is_open());
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

void verifyRegistrationNoBareNew(const std::string& filePath) {
  const std::string content = readFile(filePath);
  // Match 'new' that is NOT followed by '(std::nothrow)' or part of a comment
  // Clean comments first
  std::string cleaned;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    auto commentPos = line.find("//");
    if (commentPos != std::string::npos) {
      line = line.substr(0, commentPos);
    }
    cleaned += line + "\n";
  }

  // A bare allocation has a type directly after `new`; nothrow allocation has
  // the placement argument `(std::nothrow)` first. This also avoids treating
  // `#include <new>` as an allocation expression.
  std::regex bareNewRegex(R"(\bnew\s+[A-Za-z_:])");
  std::smatch match;
  bool foundBareNew = std::regex_search(cleaned, match, bareNewRegex);
  CHECK_MESSAGE(!foundBareNew, "File " << filePath << " contains bare 'new' without std::nothrow");
}

}  // namespace

TEST_CASE("Reader factory registration files do not use bare new") {
  const std::vector<std::string> registrationFiles = {
      "src/features/txt/Registration.cpp",
      "src/features/xtc/Registration.cpp",
      "src/features/epub/Registration.cpp",
      "src/features/markdown/Registration.cpp",
  };

  for (const auto& file : registrationFiles) {
    verifyRegistrationNoBareNew(file);
  }
}

// Regression: ReaderRegistry::open had three distinct refusals that all did a
// bare `return {}`. That default-constructs logMessage = nullptr, and
// ActivityManager::goToReader logged only `if (result.logMessage)` -- so a book
// that would not open bounced back to Home with nothing at all on serial.
//
// These two checks together pin that down: the first proves a bare `return {}`
// really is silent, the second proves open() no longer contains one.
// open() itself is not directly callable here -- it takes live GfxRenderer and
// MappedInputManager references, neither of which the host suite links.

TEST_CASE("A default-constructed ReaderOpenResult carries no reason") {
  const core::ReaderOpenResult defaulted;
  CHECK(defaulted.status == core::ReaderOpenResult::Status::LoadFailed);
  CHECK(defaulted.activity == nullptr);
  // This is exactly why `return {}` must never be used for a real refusal.
  CHECK(defaulted.logMessage == nullptr);
}

TEST_CASE("ReaderRegistry::open has no reasonless failure return") {
  const std::string content = readFile("src/core/registries/ReaderRegistry.h");

  const size_t openPos = content.find("static ReaderOpenResult open(");
  REQUIRE(openPos != std::string::npos);
  const std::string openBody = content.substr(openPos);

  std::regex bareReturnRegex(R"(return\s*\{\s*\}\s*;)");
  std::smatch match;
  CHECK_MESSAGE(!std::regex_search(openBody, match, bareReturnRegex),
                "ReaderRegistry::open contains a bare 'return {};' -- every failure needs a logMessage");
}

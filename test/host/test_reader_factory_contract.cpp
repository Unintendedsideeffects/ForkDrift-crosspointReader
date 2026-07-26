#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

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

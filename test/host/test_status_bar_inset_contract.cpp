#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"

// The global status bar is painted over the top topInset() px of every screen
// after render. Themed screens get that inset through metrics.topPadding; a
// screen that draws its own title at a literal y lands under the bar. This
// has happened twice (OPDS + reader menu, then 14 more screens), so new
// hand-rolled titles at a fixed y are rejected here.
TEST_CASE("no activity draws its title at a fixed y under the global status bar") {
  const std::vector<std::string> fixedTitlePatterns = {
      "drawCenteredText(UI_12_FONT_ID, 15,",
      "drawCenteredText(UI_12_FONT_ID, 10,",
      "drawCenteredText(UI_12_FONT_ID, 20,",
  };
  std::vector<std::string> offenders;
  for (const auto& entry : std::filesystem::recursive_directory_iterator("src/activities")) {
    if (!entry.is_regular_file() || entry.path().extension() != ".cpp") continue;
    std::ifstream source(entry.path());
    std::ostringstream buffer;
    buffer << source.rdbuf();
    const std::string text = buffer.str();
    for (const auto& pattern : fixedTitlePatterns) {
      if (text.find(pattern) != std::string::npos) offenders.push_back(entry.path().string());
    }
  }
  INFO("offenders: " << offenders.size() << (offenders.empty() ? "" : (" first: " + offenders.front())));
  CHECK(offenders.empty());
}

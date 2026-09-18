#include "doctest/doctest.h"

namespace {

int restorePageAfterReflow(const int cachedPage, const int cachedTotal, const int newTotal, const int currentSpine,
                           const int cachedSpine) {
  int currentPage = cachedPage;
  if (currentPage < 0) {
    currentPage = 0;
  } else if (newTotal > 0 && currentPage >= newTotal) {
    currentPage = newTotal - 1;
  }

  if (cachedTotal > 0) {
    if (currentSpine == cachedSpine && newTotal != cachedTotal) {
      const float progress = static_cast<float>(cachedPage) / static_cast<float>(cachedTotal);
      int newPage = static_cast<int>(progress * static_cast<float>(newTotal));
      if (newPage < 0) {
        newPage = 0;
      } else if (newTotal > 0 && newPage >= newTotal) {
        newPage = newTotal - 1;
      }
      currentPage = newPage;
    }
  }
  return currentPage;
}

int restorePageAfterReflowBuggy(const int cachedPage, const int cachedTotal, const int newTotal) {
  int currentPage = cachedPage;
  if (currentPage < 0) {
    currentPage = 0;
  } else if (newTotal > 0 && currentPage >= newTotal) {
    currentPage = newTotal - 1;
  }
  if (cachedTotal > 0 && newTotal != cachedTotal) {
    const float progress = static_cast<float>(currentPage) / static_cast<float>(cachedTotal);
    currentPage = static_cast<int>(progress * static_cast<float>(newTotal));
  }
  return currentPage;
}

}  // namespace

TEST_CASE("position preserve shrinks a 100-page chapter at page 40 to 50 pages") {
  CHECK(restorePageAfterReflow(40, 100, 50, 3, 3) == 20);
}

TEST_CASE("position preserve grows a 100-page chapter at page 40 to 200 pages") {
  CHECK(restorePageAfterReflow(40, 100, 200, 3, 3) == 80);
}

TEST_CASE("position preserve skips ratio restore when spine index differs") {
  CHECK(restorePageAfterReflow(40, 100, 50, 4, 3) == 40);
}

TEST_CASE("position preserve clamps when the scaled page meets or exceeds the new count") {
  CHECK(restorePageAfterReflow(100, 100, 50, 3, 3) == 49);
}

TEST_CASE("position preserve does not use the post-clamp page for the ratio") {
  CHECK(restorePageAfterReflow(80, 100, 50, 3, 3) == 40);
  CHECK(restorePageAfterReflowBuggy(80, 100, 50) == 24);
}

#include "doctest/doctest.h"
#include "lib/FsHelpers/FsHelpers.h"
#include "src/util/ForkDriftNavigation.h"
#include "test/mock/Arduino.h"

MockESP ESP;

TEST_CASE("testPathNormalisation") {
  CHECK(FsHelpers::normalisePath("/a/b/c") == "a/b/c");
  CHECK(FsHelpers::normalisePath("a/b/../c") == "a/c");
  CHECK(FsHelpers::normalisePath("a/b/../../c") == "c");
  CHECK(FsHelpers::normalisePath("///a//b/") == "a/b");
  CHECK(FsHelpers::normalisePath("test/../test/dir") == "test/dir");
  CHECK(FsHelpers::normalisePath("a/b/.") == "a/b");
  CHECK(FsHelpers::normalisePath("a/b/..") == "a");
  CHECK(FsHelpers::normalisePath("a/./b/../c/.") == "a/c");
}

TEST_CASE("testForkDriftCoverNavigation") {
  using ForkDriftNavigation::navigateCoverGrid;

  constexpr int cols = 3;
  constexpr int rows = 2;

  // ── Single-row: 1 book ────────────────────────────────────────────────
  {
    // Left/right wrap within the single book (stays at 0)
    auto r = navigateCoverGrid(0, 1, cols, rows, true, false, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));
    r = navigateCoverGrid(0, 1, cols, rows, false, true, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));
    // Up from row 0 → button grid
    r = navigateCoverGrid(0, 1, cols, rows, false, false, true, false);
    CHECK(r.enterButtonGrid);
    // Down → button grid (last row = row 0)
    r = navigateCoverGrid(0, 1, cols, rows, false, false, false, true);
    CHECK(r.enterButtonGrid);
  }

  // ── Single-row: 2 books ───────────────────────────────────────────────
  {
    // Right from 0 → 1
    auto r = navigateCoverGrid(0, 2, cols, rows, false, true, false, false);
    CHECK((r.bookIndex == 1 && !r.enterButtonGrid));
    // Right from 1 wraps to 0
    r = navigateCoverGrid(1, 2, cols, rows, false, true, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));
    // Left from 0 wraps to 1
    r = navigateCoverGrid(0, 2, cols, rows, true, false, false, false);
    CHECK((r.bookIndex == 1 && !r.enterButtonGrid));
    // Left from 1 → 0
    r = navigateCoverGrid(1, 2, cols, rows, true, false, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));
    // Up / Down → button grid
    r = navigateCoverGrid(0, 2, cols, rows, false, false, true, false);
    CHECK(r.enterButtonGrid);
    r = navigateCoverGrid(1, 2, cols, rows, false, false, false, true);
    CHECK(r.enterButtonGrid);
  }

  // ── Single-row: 3 books ───────────────────────────────────────────────
  {
    auto r = navigateCoverGrid(2, 3, cols, rows, false, true, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));  // wraps to start
    r = navigateCoverGrid(0, 3, cols, rows, true, false, false, false);
    CHECK((r.bookIndex == 2 && !r.enterButtonGrid));  // wraps to end
    r = navigateCoverGrid(1, 3, cols, rows, false, false, false, true);
    CHECK(r.enterButtonGrid);  // row 0 is last book row
    r = navigateCoverGrid(1, 3, cols, rows, false, false, true, false);
    CHECK(r.enterButtonGrid);  // row 0, up → button grid
  }

  // ── Two rows: 4 books ─────────────────────────────────────────────────
  {
    // Down from row 0 (book 0) → row 1 (book 3, clamped)
    auto r = navigateCoverGrid(0, 4, cols, rows, false, false, false, true);
    CHECK((r.bookIndex == 3 && !r.enterButtonGrid));
    // Down from row 0 col 1 (book 1) → row 1 col 1 clamped to 3
    r = navigateCoverGrid(1, 4, cols, rows, false, false, false, true);
    CHECK((r.bookIndex == 3 && !r.enterButtonGrid));
    // Down from row 1 (book 3) → button grid
    r = navigateCoverGrid(3, 4, cols, rows, false, false, false, true);
    CHECK(r.enterButtonGrid);
    // Up from row 1 (book 3) → row 0 col 0 (book 0)
    r = navigateCoverGrid(3, 4, cols, rows, false, false, true, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));
    // Up from row 0 → button grid
    r = navigateCoverGrid(2, 4, cols, rows, false, false, true, false);
    CHECK(r.enterButtonGrid);
    // Linear left/right wrap across rows
    r = navigateCoverGrid(3, 4, cols, rows, false, true, false, false);
    CHECK((r.bookIndex == 0 && !r.enterButtonGrid));  // 3+1 wraps to 0
    r = navigateCoverGrid(0, 4, cols, rows, true, false, false, false);
    CHECK((r.bookIndex == 3 && !r.enterButtonGrid));  // 0-1 wraps to 3
  }

  // ── Full grid: 6 books ────────────────────────────────────────────────
  {
    // Down from row 0 col 2 (book 2) → row 1 col 2 (book 5)
    auto r = navigateCoverGrid(2, 6, cols, rows, false, false, false, true);
    CHECK((r.bookIndex == 5 && !r.enterButtonGrid));
    // Down from row 1 → button grid
    r = navigateCoverGrid(4, 6, cols, rows, false, false, false, true);
    CHECK(r.enterButtonGrid);
    // Up from row 0 → button grid
    r = navigateCoverGrid(1, 6, cols, rows, false, false, true, false);
    CHECK(r.enterButtonGrid);
    // Up from row 1 → row 0
    r = navigateCoverGrid(5, 6, cols, rows, false, false, true, false);
    CHECK((r.bookIndex == 2 && !r.enterButtonGrid));
  }
}

TEST_CASE("ForkDriftNavigation menu grid uses two columns without leaving the menu row") {
  using ForkDriftNavigation::navigateMenuGrid;

  constexpr int cols = 2;

  auto r = navigateMenuGrid(0, 4, cols, false, true, false, false);
  CHECK((r.menuIndex == 1 && !r.exitToCoverGrid));
  r = navigateMenuGrid(1, 4, cols, false, false, false, true);
  CHECK((r.menuIndex == 3 && !r.exitToCoverGrid));
  r = navigateMenuGrid(3, 4, cols, false, false, true, false);
  CHECK((r.menuIndex == 1 && !r.exitToCoverGrid));
  r = navigateMenuGrid(0, 4, cols, false, false, true, false);
  CHECK(r.exitToCoverGrid);
}

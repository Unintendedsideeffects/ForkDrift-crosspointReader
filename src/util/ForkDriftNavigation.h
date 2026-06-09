#pragma once

/**
 * Pure, hardware-free navigation helpers for the ForkDrift home screen.
 *
 * Keeping this logic separate from HomeActivity lets it be unit-tested
 * on the host without any hardware mock dependencies.
 */
namespace ForkDriftNavigation {

struct CoverNavResult {
  int bookIndex;
  bool enterButtonGrid;
};

/**
 * Compute the next selection state when a directional button is pressed
 * while the user is in the cover grid.
 *
 * Rules:
 *  - LEFT / RIGHT  : linear wrap through [0, bookCount)
 *  - UP from row 0 : enter button grid (nothing above the top row)
 *  - UP from row>0 : move one row up, clamp column to last available book
 *  - DOWN from last book row : enter button grid
 *  - DOWN otherwise : move one row down, clamp column to last available book
 *
 * @param selectedIndex  Current book index [0, bookCount)
 * @param bookCount      Total number of books (must be > 0)
 * @param cols           Number of columns in the grid (e.g. 3)
 * @param rows           Number of rows in the grid (e.g. 2); unused but kept for clarity
 * @param left/right/up/down  Exactly one should be true.
 * @return CoverNavResult with the new bookIndex and whether to switch to the button grid.
 */
CoverNavResult navigateCoverGrid(int selectedIndex, int bookCount, int cols, int rows, bool left, bool right, bool up,
                                 bool down);

struct MenuNavResult {
  int menuIndex;
  bool exitToCoverGrid;
};

MenuNavResult navigateMenuGrid(int selectedIndex, int itemCount, int cols, bool left, bool right, bool up, bool down);

}  // namespace ForkDriftNavigation

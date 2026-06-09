#include "util/ForkDriftNavigation.h"

#include <algorithm>

namespace ForkDriftNavigation {

CoverNavResult navigateCoverGrid(int selectedIndex, int bookCount, int cols, int /*rows*/, bool left, bool right,
                                 bool up, bool down) {
  if (bookCount <= 0 || cols <= 0) return CoverNavResult{0, false};

  selectedIndex = std::max(0, std::min(selectedIndex, bookCount - 1));

  if (left) {
    return CoverNavResult{(selectedIndex + bookCount - 1) % bookCount, false};
  }
  if (right) {
    return CoverNavResult{(selectedIndex + 1) % bookCount, false};
  }

  const int row = selectedIndex / cols;
  const int col = selectedIndex % cols;
  const int lastBookRow = (bookCount - 1) / cols;

  if (up) {
    if (row == 0) {
      return CoverNavResult{selectedIndex, true};
    }
    const int newIndex = std::min((row - 1) * cols + col, bookCount - 1);
    return CoverNavResult{std::max(0, newIndex), false};
  }

  if (down) {
    if (row >= lastBookRow) {
      return CoverNavResult{selectedIndex, true};
    }
    const int newIndex = std::min((row + 1) * cols + col, bookCount - 1);
    return CoverNavResult{std::max(0, newIndex), false};
  }

  return CoverNavResult{selectedIndex, false};
}

MenuNavResult navigateMenuGrid(int selectedIndex, int itemCount, int cols, bool left, bool right, bool up, bool down) {
  if (itemCount <= 0 || cols <= 0) {
    return MenuNavResult{0, false};
  }

  selectedIndex = std::max(0, std::min(selectedIndex, itemCount - 1));

  const int row = selectedIndex / cols;
  const int col = selectedIndex % cols;
  const int lastRow = (itemCount - 1) / cols;

  if (left) {
    if (col > 0) {
      return MenuNavResult{selectedIndex - 1, false};
    }
    return MenuNavResult{selectedIndex, false};
  }
  if (right) {
    if (col < cols - 1 && selectedIndex + 1 < itemCount) {
      return MenuNavResult{selectedIndex + 1, false};
    }
    return MenuNavResult{selectedIndex, false};
  }
  if (up) {
    if (row == 0) {
      return MenuNavResult{selectedIndex, true};
    }
    return MenuNavResult{selectedIndex - cols, false};
  }
  if (down) {
    if (row < lastRow && selectedIndex + cols < itemCount) {
      return MenuNavResult{selectedIndex + cols, false};
    }
    return MenuNavResult{selectedIndex, false};
  }

  return MenuNavResult{selectedIndex, false};
}

}  // namespace ForkDriftNavigation

#pragma once

#include <cstddef>
#include <vector>

#include "components/TabStrip.h"

namespace books_tab_model {

using BooksTab = TabStripItem;

// The unified library strip: Recent | Files | OPDS | Settings, all selectable.
// (The former inert "Books"/placeholder tabs were folded away — Recent is the
// recent-books grid, Files/Settings are now wired to real views.)
inline std::vector<BooksTab> make(const char* recent, const char* files, const char* opds, const char* settings) {
  return {{recent, true}, {files, true}, {opds, true}, {settings, true}};
}

inline size_t selectableCount(const std::vector<BooksTab>& tabs) {
  size_t count = 0;
  for (const auto& tab : tabs) {
    if (tab.enabled) {
      ++count;
    }
  }
  return count;
}

inline size_t move(const std::vector<BooksTab>& tabs, size_t current, int delta) {
  if (tabs.empty() || selectableCount(tabs) == 0) {
    return 0;
  }

  size_t selected = current < tabs.size() && tabs[current].enabled ? current : 0;
  while (selected < tabs.size() && !tabs[selected].enabled) {
    ++selected;
  }
  if (selected >= tabs.size()) {
    return 0;
  }

  const int steps = delta < 0 ? -delta : delta;
  const int direction = delta < 0 ? -1 : 1;
  for (int step = 0; step < steps; ++step) {
    size_t candidate = selected;
    do {
      if (direction < 0) {
        candidate = candidate == 0 ? tabs.size() - 1 : candidate - 1;
      } else {
        candidate = (candidate + 1) % tabs.size();
      }
    } while (!tabs[candidate].enabled);
    selected = candidate;
  }
  return selected;
}

}  // namespace books_tab_model

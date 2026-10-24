#pragma once

/*
 * Live-or-cached chapter page/total for reader chrome after Section is released.
 * pageIndex is 0-based; displayPage() is the 1-based number shown in the menu.
 */
struct ChapterPosition {
  int pageIndex = 0;
  int totalPages = 0;

  constexpr int displayPage() const { return pageIndex + 1; }
  constexpr bool hasTotal() const { return totalPages > 0; }
  constexpr float chapterFraction() const {
    return hasTotal() ? static_cast<float>(pageIndex) / static_cast<float>(totalPages) : 0.0f;
  }
};

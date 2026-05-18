#pragma once

namespace features::status_overlay {

// Reading-context fields published by the active reader for the *single* status
// bar to render. The global status bar is one entity: the overlay owns the band
// and (being a post-render hook that clears the band last) is the only path that
// can draw into it. Readers therefore publish here instead of drawing their own
// second bar. Fixed buffers — no per-page heap churn (CLAUDE.md String Policy).
struct ReaderContext {
  bool active = false;             // a reader page is currently shown
  char progress[28] = "";          // e.g. "123/456  78%" — empty hides it
  char title[160] = "";            // resolved chapter/book title — empty hides it
  int progressBarPercent = -1;     // 0..100 draws the progress edge; <0 hides it
  int progressBarThicknessPx = 2;  // progress edge thickness in pixels

  static ReaderContext& get();

  void clear() {
    active = false;
    progress[0] = '\0';
    title[0] = '\0';
    progressBarPercent = -1;
    progressBarThicknessPx = 2;
  }
};

inline void clearReaderContext() { ReaderContext::get().clear(); }

}  // namespace features::status_overlay

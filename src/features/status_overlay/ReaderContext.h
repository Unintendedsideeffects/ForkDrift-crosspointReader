#pragma once

namespace features::status_overlay {

// Reading-context fields published by the active reader for the *single* status
// bar to render. The global status bar is one entity: the overlay owns the band
// and (being a post-render hook that clears the band last) is the only path that
// can draw into it. Readers therefore publish here instead of drawing their own
// second bar. Fixed buffers — no per-page heap churn (CLAUDE.md String Policy).
struct ReaderContext {
  bool active = false;
  bool pageBookmarked = false;
  char progress[28] = "";
  char title[160] = "";
  int progressBarPercent = -1;
  int progressBarThicknessPx = 2;

  static ReaderContext& get();

  void clear() {
    active = false;
    pageBookmarked = false;
    progress[0] = '\0';
    title[0] = '\0';
    progressBarPercent = -1;
    progressBarThicknessPx = 2;
  }
};

inline void clearReaderContext() { ReaderContext::get().clear(); }

}  // namespace features::status_overlay

#pragma once

namespace features::status_overlay {

struct ReaderContext {
  bool active = false;
  char progress[28] = "";
  char title[160] = "";
  int progressBarPercent = -1;
  int progressBarThicknessPx = 2;

  static ReaderContext& get() {
    static ReaderContext instance;
    return instance;
  }

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

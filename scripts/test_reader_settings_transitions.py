#!/usr/bin/env python3
"""Exercise production settings-result and stats-transition methods on the host.

Like test_clipping_page_matcher.py, compile actual method bodies with small
fixtures so activity lifecycle transitions can run without hardware or a UI.
"""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


FIXTURES = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include "activities/ActivityResult.h"

struct ResultActivity {
  bool ttfRenderingChanged = false;
  bool finished = false;
  ActivityResult result;
  void setResult(ActivityResult value) { result = std::move(value); }
  void finish() { finished = true; }
};
struct SettingsActivity : ResultActivity { void finishToParent(); };
struct ReaderOptionsActivity : ResultActivity { void finishWithResult(bool cancelled); };

struct Date {
  int value = 0;
  bool isValid() const { return value != 0; }
};
struct ReadingStatsDateTime { Date date{1}; };
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& result) {
  result.date.value = 2;
  return true;
}
unsigned long now = 10000;
unsigned long millis() { return now; }
struct Settings {
  bool enabled = true;
  bool shouldTrackReadingStats() const { return enabled; }
  uint32_t getReadingIdleTimeThresholdSeconds() const { return 300; }
} SETTINGS;
struct Stats {
  uint32_t totalReadingSeconds = 100, sessionCount = 1, totalSessions = 1;
  uint32_t spanSeconds = 0, savedSeconds = 0;
  int saves = 0;
  bool saveFails = false, startDateManual = false;
  Date startDate;
  void recordReadingSpan(const ReadingStatsDateTime& start, uint32_t seconds) {
    assert(start.date.value == (spanSeconds == 0 ? 1 : 2));
    spanSeconds += seconds;
  }
  bool save(const std::string& = "") {
    ++saves;
    if (saveFails) return false;
    savedSeconds = totalReadingSeconds;
    return true;
  }
};
struct Book { std::string getCachePath() const { return "/book"; } } book;
struct ReaderState {
  Book* epub = &book;
  Book* xtc = &book;
  bool section = true, activeFootnotePreview = false;
  bool bookStatsEnabled = true, statsTrackingActive = true;
  bool pendingStatsCommit = false, paceDirty = false;
  bool hasSessionStartLocalDateTime = true;
  ReadingStatsDateTime sessionStartLocalDateTime;
  unsigned long pageShownAtMs = 1000;
  uint32_t sessionReadingSeconds = 111;
  Stats stats, globalStats;
  void armReadingPaceWarmup(const char*) {}
};
struct EpubReaderActivity : ReaderState {
  void syncStatsTrackingState();
  void commitReadingStatsSession();
  bool currentPageReadingSecondsForStats(uint32_t&, const char*) const;
  void recordCurrentPageReadingTime(const char*);
};
struct XtcReaderActivity : ReaderState {
  void syncStatsTrackingState();
  void commitReadingStats();
  bool currentPageReadingSecondsForStats(uint32_t&, const char*) const;
  void recordCurrentPageReadingTime(const char*);
};
'''

CASES = r'''
template <typename Reader> void checkStats(bool globalToggle) {
  SETTINGS.enabled = true;
  Reader reader;
  if (globalToggle) SETTINGS.enabled = false;
  else reader.bookStatsEnabled = false;
  reader.syncStatsTrackingState();
  assert(!reader.statsTrackingActive);
  assert(reader.sessionReadingSeconds == 0);
  // 111 seconds on previous pages plus 9 seconds on the current page.
  assert(reader.stats.totalReadingSeconds == 220);
  assert(reader.globalStats.totalReadingSeconds == 220);
  assert(reader.stats.savedSeconds == 220 && reader.globalStats.savedSeconds == 220);
  assert(reader.stats.sessionCount == 2 && reader.globalStats.totalSessions == 2);
  assert(reader.stats.spanSeconds == 120 && reader.globalStats.spanSeconds == 120);
  assert(reader.stats.startDate.value == 1);
  reader.syncStatsTrackingState();
  assert(reader.stats.totalReadingSeconds == 220);
  SETTINGS.enabled = true;
  reader.bookStatsEnabled = true;
  reader.syncStatsTrackingState();
  assert(reader.statsTrackingActive && reader.sessionReadingSeconds == 0);
  now += 60000;
  reader.bookStatsEnabled = false;
  reader.syncStatsTrackingState();
  assert(reader.stats.totalReadingSeconds == 280);
  assert(reader.globalStats.totalReadingSeconds == 280);
  assert(reader.stats.sessionCount == 3 && reader.globalStats.totalSessions == 3);
  assert(reader.stats.spanSeconds == 180 && reader.globalStats.spanSeconds == 180);
  assert(reader.stats.startDate.value == 1);
  now = 10000;
}

template <typename Reader> void checkThresholds() {
  SETTINGS.enabled = true;
  for (uint32_t seconds : {9, 10, 59, 60}) {
    Reader reader;
    reader.sessionReadingSeconds = seconds;
    reader.pageShownAtMs = 0;  // Menu already paused the page timer.
    reader.bookStatsEnabled = false;
    reader.syncStatsTrackingState();
    assert(reader.stats.totalReadingSeconds == 100 + (seconds >= 10 ? seconds : 0));
    assert(reader.globalStats.totalReadingSeconds == reader.stats.totalReadingSeconds);
    assert(reader.stats.sessionCount == (seconds >= 60 ? 2 : 1));
    assert(reader.globalStats.totalSessions == reader.stats.sessionCount);
  }
}

template <typename Reader> void checkIdleAndFailure() {
  SETTINGS.enabled = true;
  Reader reader;
  reader.pageShownAtMs = 1000;
  now = 400000;  // Idle page is excluded; already recorded reading survives.
  reader.stats.saveFails = true;
  reader.bookStatsEnabled = false;
  reader.syncStatsTrackingState();
  assert(reader.stats.totalReadingSeconds == 211);
  assert(reader.globalStats.totalReadingSeconds == 211);
  assert(reader.pendingStatsCommit);
  assert(reader.sessionReadingSeconds == 0);
  reader.syncStatsTrackingState();
  assert(reader.stats.totalReadingSeconds == 211);
  now = 10000;
}

int main(int argc, char**) {
  if (argc > 1) {
    for (bool changed : {false, true}) {
      SettingsActivity settings;
      settings.ttfRenderingChanged = changed;
      settings.finishToParent();
      auto result = std::get<TtfRenderOptionsResult>(settings.result.data);
      assert(settings.finished && result.changed == changed && result.activeFamilyChanged == changed);
      for (bool cancelled : {false, true}) {
        ReaderOptionsActivity options;
        options.ttfRenderingChanged = changed;
        options.finishWithResult(cancelled);
        result = std::get<TtfRenderOptionsResult>(options.result.data);
        assert(options.finished && options.result.isCancelled == cancelled);
        assert(result.changed == changed && result.activeFamilyChanged == changed);
      }
    }
  } else {
    for (bool global : {false, true}) {
      checkStats<EpubReaderActivity>(global);
      checkStats<XtcReaderActivity>(global);
    }
    checkThresholds<EpubReaderActivity>();
    checkThresholds<XtcReaderActivity>();
    checkIdleAndFailure<EpubReaderActivity>();
    checkIdleAndFailure<XtcReaderActivity>();
  }
}
'''


def main():
    methods = method("src/activities/settings/SettingsActivity.cpp",
                     "void SettingsActivity::finishToParent()")
    methods += method("src/activities/reader/ReaderOptionsActivity.cpp",
                      "void ReaderOptionsActivity::finishWithResult(")
    for reader in ("EpubReaderActivity", "XtcReaderActivity"):
        path = f"src/activities/reader/{reader}.cpp"
        for name, result in (("syncStatsTrackingState", "void"),
                             ("currentPageReadingSecondsForStats", "bool"),
                             ("recordCurrentPageReadingTime", "void")):
            methods += method(path, f"{result} {reader}::{name}(")
        if reader == "XtcReaderActivity":
            methods += method(path, f"void {reader}::commitReadingStats()")
        else:
            methods += method(path, "void EpubReaderActivity::commitReadingStatsSession()")
    with tempfile.TemporaryDirectory(prefix="crossink-reader-transitions-") as directory:
        source = Path(directory) / "transitions.cpp"
        binary = Path(directory) / "transitions"
        source.write_text(FIXTURES + methods + CASES)
        subprocess.run(["c++", "-std=c++20", "-I" + str(ROOT / "src"),
                        str(source), "-o", str(binary)], check=True)
        results = [subprocess.run([str(binary), *args]).returncode for args in ([], ["fonts"])]
        assert results == [0, 0], f"Stats and font transition exit codes: {results}"
    print("PASS: font results; book/global stats toggles, re-enable, idle time, and failed saves")


if __name__ == "__main__":
    main()

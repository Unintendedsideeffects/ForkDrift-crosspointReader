#pragma once

#include <FeatureFlags.h>

#if ENABLE_ANNOTATIONS

#include <cstdint>
#include <string>
#include <vector>

// Persistent in-book highlights, created from the reader's text-selection mode.
// Inspired by the Inx firmware's annotations (github.com/obijuankenobiii/inx,
// MIT) with a more relayout-tolerant key: entries carry the highlighted TEXT as
// ground truth alongside page/word indices. When the section cache regenerates
// (font/margin change), stale indices simply stop matching and the highlight is
// re-anchored by word-sequence match — or shown only in text form, never
// crashing or highlighting the wrong words.
//
// Storage: <book cache dir>/annotations.bin, small binary records, one file per
// book, loaded whole (annotation counts are tens, not thousands; enforced cap).
struct Annotation {
  uint16_t spineIndex = 0;
  uint16_t page = 0;       // page at creation time (hint only)
  uint16_t startWord = 0;  // flat selectable-word index on that page (hint only)
  uint16_t endWord = 0;
  std::string text;  // ground truth, space-joined selected words
};

class AnnotationStore {
 public:
  static constexpr size_t kMaxAnnotationsPerBook = 200;
  static constexpr size_t kMaxTextBytes = 1024;

  static AnnotationStore& getInstance() {
    static AnnotationStore instance;
    return instance;
  }

  // Load the store for a book cache dir; empty store when the file is missing.
  bool loadForBook(const std::string& cachePath);
  void unload();

  bool add(const Annotation& annotation);
  // Remove every annotation on (spine, page) whose [startWord, endWord] range
  // contains wordIdx. Returns the number removed.
  int removeAt(uint16_t spineIndex, uint16_t page, uint16_t wordIdx);

  bool hasAnyFor(uint16_t spineIndex, uint16_t page) const;
  // True when some annotation on (spine, page) contains wordIdx in its range.
  bool removeCandidateAt(uint16_t spineIndex, uint16_t page, uint16_t wordIdx) const;
  // Annotations on a given page (hint indices; callers must bounds-check).
  std::vector<const Annotation*> forPage(uint16_t spineIndex, uint16_t page) const;

  const std::vector<Annotation>& all() const { return annotations; }
  void saveToFile();

 private:
  std::string filePath;
  std::vector<Annotation> annotations;
  bool dirty = false;
  bool loaded = false;
};

#define ANNOTATIONS AnnotationStore::getInstance()

#endif  // ENABLE_ANNOTATIONS

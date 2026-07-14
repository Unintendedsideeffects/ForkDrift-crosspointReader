#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace selection {

struct SelWord {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  std::string text;
};

struct Model {
  std::vector<SelWord> words;
  int cursor = 0;
  int anchor = 0;
  bool anchored = false;
};

void move(Model& model, int delta);
bool stepBack(Model& model);
std::pair<int, int> span(const Model& model);
std::string joinSpan(const std::vector<SelWord>& words, int lo, int hi);
// Reconstructs the sentence surrounding the selected span [lo, hi] from the
// page word list. Expands left/right to sentence boundaries (a word ending in
// '.', '!' or '?'), then joins whole words up to the soft `maxChars` cap. The
// selected span is always retained, even if it alone exceeds the cap, and no
// UTF-8 codepoint is split.
std::string sentenceSpan(const std::vector<SelWord>& words, int lo, int hi, size_t maxChars = 240);
bool anchorByText(const std::vector<SelWord>& words, const std::string& text, int hintLo, int hintHi, int& outLo,
                  int& outHi);
// Like anchorByText's slide search, but reports whether the text occurs more
// than once among `words`. Returns false when there is no match at all.
// `unique` is set true only when exactly one occurrence exists.
bool anchorByTextUnique(const std::vector<SelWord>& words, const std::string& text, int spanLen, int& outLo, int& outHi,
                        bool& unique);

}  // namespace selection

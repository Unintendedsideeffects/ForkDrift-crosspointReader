#include "SelectionModel.h"

#include <algorithm>

namespace selection {
namespace {

std::string firstWord(const std::string& text) {
  const size_t end = text.find(' ');
  return text.substr(0, end);
}

}  // namespace

void move(Model& model, const int delta) {
  const int count = static_cast<int>(model.words.size());
  model.cursor = std::clamp(model.cursor + delta, 0, count - 1);
  if (!model.anchored) {
    model.anchor = model.cursor;
  }
}

bool stepBack(Model& model) {
  if (model.anchored) {
    model.anchored = false;
    model.cursor = model.anchor;
    return true;
  }
  return false;
}

std::pair<int, int> span(const Model& model) {
  return {std::min(model.anchor, model.cursor), std::max(model.anchor, model.cursor)};
}

std::string joinSpan(const std::vector<SelWord>& words, const int lo, const int hi) {
  std::string out;
  for (int i = lo; i <= hi && i < static_cast<int>(words.size()); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words[static_cast<size_t>(i)].text;
  }
  return out;
}

bool anchorByText(const std::vector<SelWord>& words, const std::string& text, const int hintLo, const int hintHi,
                  int& outLo, int& outHi) {
  const int count = static_cast<int>(words.size());
  if (hintLo > hintHi) {
    return false;
  }
  if (hintHi < count && joinSpan(words, hintLo, hintHi) == text) {
    outLo = hintLo;
    outHi = hintHi;
    return true;
  }

  const int spanLen = hintHi - hintLo;
  const std::string first = firstWord(text);
  if (first.empty()) {
    return false;
  }
  for (int start = 0; start + spanLen < count; ++start) {
    if (words[static_cast<size_t>(start)].text != first) {
      continue;
    }
    if (joinSpan(words, start, start + spanLen) == text) {
      outLo = start;
      outHi = start + spanLen;
      return true;
    }
  }
  return false;
}

}  // namespace selection

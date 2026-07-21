#include "SelectionModel.h"

#include <algorithm>
#include <cstdint>

namespace selection {
namespace {

std::string firstWord(const std::string& text) {
  const size_t end = text.find(' ');
  return text.substr(0, end);
}

// True when `text` is the last word of a sentence, i.e. its final visible
// character (ignoring trailing closing quotes/brackets) is a sentence
// terminator. Only ASCII closers are stripped; unicode closers simply leave a
// non-terminator byte, which safely errs toward keeping the sentence together.
bool endsSentence(const std::string& text) {
  size_t end = text.size();
  while (end > 0) {
    const char c = text[end - 1];
    if (c == '"' || c == '\'' || c == ')' || c == ']' || c == '}') {
      --end;
    } else {
      break;
    }
  }
  if (end == 0) {
    return false;
  }
  const char last = text[end - 1];
  return last == '.' || last == '!' || last == '?';
}

size_t joinedLength(const std::vector<SelWord>& words, const int lo, const int hi) {
  size_t length = 0;
  for (int i = lo; i <= hi; ++i) {
    if (i > lo) {
      ++length;
    }
    length += words[static_cast<size_t>(i)].text.size();
  }
  return length;
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

std::string joinSpanFormatted(const std::vector<SelWord>& words, const int lo, const int hi) {
  std::string out;
  uint16_t lastLineId = UINT16_MAX;
  for (int i = lo; i <= hi && i < static_cast<int>(words.size()); ++i) {
    const SelWord& word = words[static_cast<size_t>(i)];
    if (!out.empty()) {
      out += (word.lineId != lastLineId) ? '\n' : ' ';
    }
    out += word.text;
    lastLineId = word.lineId;
  }
  return out;
}

std::string sentenceSpan(const std::vector<SelWord>& words, const int lo, const int hi, const size_t maxChars) {
  const int count = static_cast<int>(words.size());
  if (count == 0) {
    return {};
  }
  const int selectedStart = std::clamp(lo, 0, count - 1);
  const int selectedEnd = std::clamp(hi, selectedStart, count - 1);
  int start = selectedStart;
  int end = selectedEnd;

  // Grow left until the previous word closes a sentence (or we hit the page top).
  while (start > 0 && !endsSentence(words[static_cast<size_t>(start - 1)].text)) {
    --start;
  }
  // Grow right until the current word closes a sentence (or we hit the page end).
  while (end < count - 1 && !endsSentence(words[static_cast<size_t>(end)].text)) {
    ++end;
  }

  if (joinedLength(words, start, end) <= maxChars) {
    return joinSpanFormatted(words, start, end);
  }

  // Keep the entire selected span even when it appears after the initial cap.
  // Drop leading context one word at a time until the selection fits, then use
  // any remaining room for trailing context. If the selection alone is longer
  // than the soft cap, return it intact. Never slicing a word also prevents a
  // multi-byte UTF-8 codepoint from being split.
  while (start < selectedStart && joinedLength(words, start, selectedEnd) > maxChars) {
    ++start;
  }
  if (joinedLength(words, start, selectedEnd) > maxChars) {
    return joinSpanFormatted(words, selectedStart, selectedEnd);
  }

  end = selectedEnd;
  while (end < count - 1 && !endsSentence(words[static_cast<size_t>(end)].text) &&
         joinedLength(words, start, end + 1) <= maxChars) {
    ++end;
  }
  return joinSpanFormatted(words, start, end);
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

bool anchorByTextUnique(const std::vector<SelWord>& words, const std::string& text, const int spanLen, int& outLo,
                        int& outHi, bool& unique) {
  const int count = static_cast<int>(words.size());
  unique = false;
  const std::string first = firstWord(text);
  if (first.empty()) {
    return false;
  }

  int matchCount = 0;
  for (int start = 0; start + spanLen < count; ++start) {
    if (words[static_cast<size_t>(start)].text != first) {
      continue;
    }
    if (joinSpan(words, start, start + spanLen) == text) {
      if (matchCount == 0) {
        outLo = start;
        outHi = start + spanLen;
      }
      matchCount++;
    }
  }
  unique = matchCount == 1;
  return matchCount > 0;
}

std::vector<HighlightRect> buildHighlightRuns(const std::vector<SelWord>& words, const int lo, const int hi) {
  std::vector<HighlightRect> runs;
  if (words.empty() || lo > hi || lo < 0 || hi >= static_cast<int>(words.size())) {
    return runs;
  }

  int runStart = lo;
  while (runStart <= hi) {
    const uint16_t lineId = words[static_cast<size_t>(runStart)].lineId;
    int runEnd = runStart;
    while (runEnd + 1 <= hi && words[static_cast<size_t>(runEnd + 1)].lineId == lineId) {
      ++runEnd;
    }

    int16_t minX = words[static_cast<size_t>(runStart)].x;
    int16_t minY = words[static_cast<size_t>(runStart)].y;
    int16_t maxX = static_cast<int16_t>(minX + words[static_cast<size_t>(runStart)].w);
    int16_t maxY = static_cast<int16_t>(minY + words[static_cast<size_t>(runStart)].h);

    for (int i = runStart + 1; i <= runEnd; ++i) {
      const SelWord& word = words[static_cast<size_t>(i)];
      if (word.w <= 0 || word.h <= 0) {
        continue;
      }
      minX = std::min(minX, word.x);
      minY = std::min(minY, word.y);
      maxX = std::max(maxX, static_cast<int16_t>(word.x + word.w));
      maxY = std::max(maxY, static_cast<int16_t>(word.y + word.h));
    }

    if (maxX > minX && maxY > minY) {
      runs.push_back(HighlightRect{static_cast<int16_t>(minX - 1), minY, static_cast<int16_t>(maxX - minX + 2),
                                   static_cast<int16_t>(maxY - minY)});
    }

    runStart = runEnd + 1;
  }

  return runs;
}

}  // namespace selection

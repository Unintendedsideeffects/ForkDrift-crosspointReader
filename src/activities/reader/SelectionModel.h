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
bool anchorByText(const std::vector<SelWord>& words, const std::string& text, int hintLo, int hintHi, int& outLo,
                  int& outHi);

}  // namespace selection

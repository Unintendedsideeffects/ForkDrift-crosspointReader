#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <sstream>

#include "components/ScreenComponents.h"
#include "components/UITheme.h"
#include "fontIds.h"

DictionaryActivity::DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string pageWords,
                                       std::string contextText)
    : Activity("Dictionary", renderer, mappedInput),
      pageWords(std::move(pageWords)),
      contextText(std::move(contextText)) {}

void DictionaryActivity::onEnter() {
  Activity::onEnter();

  // Parse pageWords
  std::istringstream iss(pageWords);
  std::string word;
  while (iss >> word) {
    wordsList.push_back(word);
  }

  selectedIndex = 0;

  dictMissing = true;
  HalFile f;
  if (Storage.openFileForRead("DICT", "/dictionary/en.dict", f)) {
    auto heapFile = makeUniqueNoThrow<HalFile>(std::move(f));
    if (heapFile) {
      accessor = makeUniqueNoThrow<HalFileDictionaryAccessor>(std::move(heapFile));
      if (accessor) {
        dictMissing = false;
      }
    }
  }

  requestUpdate();
}

void DictionaryActivity::onExit() {
  Activity::onExit();
  wordsList.clear();
  accessor.reset();
}

void DictionaryActivity::lookupWord(const std::string& word) {
  if (dictMissing || !accessor) {
    currentDefinition = tr(STR_DICT_NOT_FOUND);
  } else {
    currentDefinition = DictionaryLookup::lookup(*accessor, word);
    if (currentDefinition.empty()) {
      currentDefinition = tr(STR_DICT_WORD_NOT_FOUND);
    }
  }

  definitionLines.clear();
  definitionLines.reserve(100);
  int margin = 20;
  int maxWidth = renderer.getScreenWidth() - 2 * margin;
  std::istringstream iss(currentDefinition);
  std::string token;
  std::string currentLine;
  while (iss >> token) {
    std::string candidate = currentLine.empty() ? token : currentLine + " " + token;
    if (renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) > maxWidth) {
      if (!currentLine.empty()) {
        definitionLines.push_back(currentLine);
        currentLine = token;
      } else {
        definitionLines.push_back(token);
        currentLine = "";
      }
    } else {
      currentLine = candidate;
    }
  }
  if (!currentLine.empty()) {
    definitionLines.push_back(currentLine);
  }

  showingDefinition = true;
  scrollLine = 0;
  requestUpdate();
}

void DictionaryActivity::loop() {
  if (showingDefinition) {
    const int topY = 70;
    const int lineH = 24;
    const int bottomY = renderer.getScreenHeight() - 60;
    int visibleLines = (bottomY - topY) / lineH;
    int maxScroll = std::max(0, (int)definitionLines.size() - visibleLines);

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      showingDefinition = false;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      scrollLine += visibleLines;
      if (scrollLine > maxScroll) scrollLine = maxScroll;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      scrollLine -= visibleLines;
      if (scrollLine < 0) scrollLine = 0;
      requestUpdate();
    }
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!wordsList.empty() && selectedIndex >= 0 && selectedIndex < (int)wordsList.size()) {
        lookupWord(wordsList[selectedIndex]);
      }
      return;
    }
    navigator.onNext([this] {
      if (!wordsList.empty()) {
        selectedIndex = (selectedIndex + 1) % (int)wordsList.size();
        requestUpdate();
      }
    });
    navigator.onPrevious([this] {
      if (!wordsList.empty()) {
        selectedIndex = (selectedIndex - 1 + (int)wordsList.size()) % (int)wordsList.size();
        requestUpdate();
      }
    });
  }
}

void DictionaryActivity::render(RenderLock&& lock) {
  if (showingDefinition) {
    renderDefinition(lock);
  } else {
    renderList(lock);
  }
}

void DictionaryActivity::renderList(RenderLock& lock) {
  renderer.clearScreen();

  renderer.drawCenteredText(UI_12_FONT_ID, 40, tr(STR_DICTIONARY), true, EpdFontFamily::BOLD);

  if (dictMissing) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120, tr(STR_DICT_NOT_FOUND));
  } else if (wordsList.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 120, tr(STR_DICT_NO_WORDS));
  } else {
    const int startY = 80;
    const int lineHeight = 40;

    for (size_t i = 0; i < wordsList.size(); ++i) {
      const int y = startY + (i * lineHeight);
      const bool isSelected = ((int)i == selectedIndex);
      if (isSelected) {
        renderer.fillRect(20, y - 10, renderer.getScreenWidth() - 40, lineHeight, true);
      }
      renderer.drawCenteredText(UI_12_FONT_ID, y + 20, wordsList[i].c_str(), !isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

void DictionaryActivity::renderDefinition(RenderLock& lock) {
  renderer.clearScreen();

  std::string title =
      (selectedIndex >= 0 && selectedIndex < (int)wordsList.size()) ? wordsList[selectedIndex] : std::string();
  renderer.drawCenteredText(UI_12_FONT_ID, 30, title.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawLine(20, 50, renderer.getScreenWidth() - 20, 50);

  const int topY = 70;
  const int lineH = 24;
  const int bottomY = renderer.getScreenHeight() - 60;
  int visibleLines = (bottomY - topY) / lineH;
  int margin = 20;

  for (int i = 0; i < visibleLines; ++i) {
    int lineIndex = scrollLine + i;
    if (lineIndex >= 0 && lineIndex < (int)definitionLines.size()) {
      renderer.drawText(UI_10_FONT_ID, margin, topY + i * lineH, definitionLines[lineIndex].c_str());
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_SCROLL), tr(STR_SCROLL));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

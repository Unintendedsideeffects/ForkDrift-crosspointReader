#include "AnkiActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#ifndef SIMULATOR
#include <esp_random.h>
#endif

#include "components/ScreenComponents.h"
#include "components/UITheme.h"
#include "fontIds.h"

AnkiActivity::AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Anki", renderer, mappedInput) {}

void AnkiActivity::onEnter() {
  Activity::onEnter();
  // Snapshot under the store's mutex so render/loop never race with web server mutations.
  cards = util::AnkiStore::getInstance().copyCards();
  showingBack = false;
  if (!cards.empty()) {
    nextSrsCard();
  } else {
    selectedIndex = 0;
  }
  requestUpdate();
}

void AnkiActivity::loop() {
  if (cards.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goHome();
    }
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
    selectedIndex = (selectedIndex + 1) % cards.size();
    showingBack = false;
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selectedIndex = (selectedIndex + cards.size() - 1) % cards.size();
    showingBack = false;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    nextSrsCard();
    showingBack = false;
    requestUpdate();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showingBack = !showingBack;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
  }
}

void AnkiActivity::nextSrsCard() {
  if (cards.size() <= 1) {
    selectedIndex = 0;
    if (cards.size() == 1) {
      util::AnkiStore::getInstance().incrementCardReadCount(0);
      util::AnkiStore::getInstance().save();
      cards[0].readCount++;
    }
    return;
  }

  const uint32_t minCount = std::min_element(cards.begin(), cards.end(), [](const auto& a, const auto& b) {
                              return a.readCount < b.readCount;
                            })->readCount;

  std::vector<size_t> candidates;
  for (size_t i = 0; i < cards.size(); ++i) {
    if (cards[i].readCount == minCount) {
      if (i != selectedIndex || candidates.empty()) {
        candidates.push_back(i);
      }
    }
  }

  if (candidates.empty()) {
    candidates.push_back(selectedIndex);
  }

  size_t randIdx;
#ifdef SIMULATOR
  randIdx = rand() % candidates.size();
#else
  randIdx = esp_random() % candidates.size();
#endif
  selectedIndex = candidates[randIdx];
  util::AnkiStore::getInstance().incrementCardReadCount(selectedIndex);
  util::AnkiStore::getInstance().save();
  cards[selectedIndex].readCount++;
}

void AnkiActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 40, tr(STR_ANKI_CARDS), true, EpdFontFamily::BOLD);

  if (cards.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_ANKI_NO_CARDS));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_ANKI_ADD_HINT));
  } else {
    const auto& card = cards[selectedIndex];

    // Progress counter
    char progress[16];
    snprintf(progress, sizeof(progress), "%d / %zu", selectedIndex + 1, cards.size());
    renderer.drawCenteredText(UI_10_FONT_ID, 70, progress);

    // Card Box
    const int boxMargin = 20;
    const int boxWidth = pageWidth - (boxMargin * 2);
    const int boxHeight = pageHeight - 200;
    const int boxY = 100;

    renderer.drawRect(boxMargin, boxY, boxWidth, boxHeight);

    if (showingBack) {
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 30, tr(STR_ANKI_BACK), false, EpdFontFamily::ITALIC);
      std::vector<std::string> lines = renderer.wrappedText(
          UI_12_FONT_ID, card.back.empty() ? tr(STR_NONE_OPT) : card.back.c_str(), boxWidth - 40, 10);
      int y = boxY + 80;
      for (const auto& line : lines) {
        renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
        y += renderer.getLineHeight(UI_12_FONT_ID);
      }

      char countText[32];
      snprintf(countText, sizeof(countText), "Read count: %u", card.readCount);
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + boxHeight - 40, countText);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 30, tr(STR_ANKI_FRONT), false, EpdFontFamily::ITALIC);
      renderer.drawCenteredText(UI_12_FONT_ID, boxY + boxHeight / 2 - 10, card.front.c_str(), true,
                                EpdFontFamily::BOLD);

      if (!card.context.empty()) {
        char ctx[128];
        snprintf(ctx, sizeof(ctx), tr(STR_ANKI_SOURCE_PREFIX), card.context.c_str());
        renderer.drawCenteredText(UI_10_FONT_ID, boxY + boxHeight - 40, ctx);
      }
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_TOGGLE), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

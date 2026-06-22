#include "AnkiActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#ifndef SIMULATOR
#include <esp_random.h>
#endif

#include "components/ScreenComponents.h"
#include "components/UITheme.h"
#include "fontIds.h"

AnkiActivity::AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Anki", renderer, mappedInput) {}

void AnkiActivity::loadDecks() {
  deckTitles.clear();
  deckPaths.clear();

  std::vector<std::string> paths;
  auto p1 = FlashcardsStore::listDecks("/flashcards");
  paths.insert(paths.end(), p1.begin(), p1.end());
  auto p2 = FlashcardsStore::listDecks("/decks");
  paths.insert(paths.end(), p2.begin(), p2.end());

  deckTitles.reserve(paths.size());
  deckPaths.reserve(paths.size());
  for (const auto& path : paths) {
    FlashcardDeck deck;
    if (FlashcardsStore::loadDeck(path, deck)) {
      deckTitles.push_back(deck.title);
      deckPaths.push_back(path);
    }
  }
}

void AnkiActivity::enterCsvDeck(size_t deckIdx) {
  if (deckIdx >= deckPaths.size()) {
    return;
  }
  std::string err;
  if (!FlashcardsStore::loadDeck(deckPaths[deckIdx], csvDeck, &err)) {
    LOG_ERR("ANKI", "Failed to load deck: %s", err.c_str());
    return;
  }
  FlashcardsStore::loadDeckProgress(csvDeck, csvProgress);

  const std::time_t now = std::time(nullptr);
  const uint32_t currentDay = now > 0 ? static_cast<uint32_t>(now / 86400) : 0u;

  csvQueue = FlashcardsStore::buildStudyQueue(csvDeck, csvProgress, FlashcardStudyMode::Due, currentDay);
  if (csvQueue.empty()) {
    csvQueue = FlashcardsStore::buildStudyQueue(csvDeck, csvProgress, FlashcardStudyMode::All, currentDay);
  }
  csvQueuePos = 0;
  csvShowingBack = false;
  mode = Mode::CsvDeck;
  requestUpdate();
}

void AnkiActivity::onEnter() {
  Activity::onEnter();
  loadDecks();

  // Snapshot reader cards under the store's mutex so render/loop never race with web server mutations.
  cards = util::AnkiStore::getInstance().copyCards();
  showingBack = false;
  selectedIndex = 0;

  // If no CSV decks exist, go straight to reader-card review (no regression for deckless users).
  if (deckPaths.empty()) {
    mode = Mode::ReaderCards;
    if (!cards.empty()) {
      nextSrsCard();
    }
  } else {
    mode = Mode::Hub;
    hubIndex = 0;
  }
  requestUpdate();
}

void AnkiActivity::onExit() { Activity::onExit(); }

void AnkiActivity::loop() {
  if (mode == Mode::Hub) {
    // Total hub entries: 1 (Reader Cards) + CSV deck count
    const int totalEntries = static_cast<int>(1 + deckPaths.size());

    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this, totalEntries] {
      hubIndex = (hubIndex + 1) % totalEntries;
      requestUpdate();
    });

    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, totalEntries] {
      hubIndex = (hubIndex + totalEntries - 1) % totalEntries;
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (hubIndex == 0) {
        mode = Mode::ReaderCards;
        showingBack = false;
        if (!cards.empty()) {
          nextSrsCard();
        }
        requestUpdate();
      } else {
        enterCsvDeck(static_cast<size_t>(hubIndex - 1));
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goHome();
    }
    return;
  }

  if (mode == Mode::ReaderCards) {
    if (cards.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        if (deckPaths.empty()) {
          activityManager.goHome();
        } else {
          mode = Mode::Hub;
          requestUpdate();
        }
      }
      return;
    }

    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down}, [this] {
      selectedIndex = (selectedIndex + 1) % cards.size();
      showingBack = false;
      requestUpdate();
    });

    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
      selectedIndex = (selectedIndex + static_cast<int>(cards.size()) - 1) % static_cast<int>(cards.size());
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
      if (deckPaths.empty()) {
        activityManager.goHome();
      } else {
        mode = Mode::Hub;
        requestUpdate();
      }
    }
    return;
  }

  if (mode == Mode::CsvDeck) {
    if (csvQueue.empty()) {
      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        mode = Mode::Hub;
        requestUpdate();
      }
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      csvShowingBack = !csvShowingBack;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
               mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      csvQueuePos = (csvQueuePos + 1) % csvQueue.size();
      csvShowingBack = false;
      requestUpdate();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = Mode::Hub;
      requestUpdate();
    }
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
      if (i != static_cast<size_t>(selectedIndex) || candidates.empty()) {
        candidates.push_back(i);
      }
    }
  }

  if (candidates.empty()) {
    candidates.push_back(static_cast<size_t>(selectedIndex));
  }

  size_t randIdx;
#ifdef SIMULATOR
  randIdx = rand() % candidates.size();
#else
  randIdx = esp_random() % candidates.size();
#endif
  selectedIndex = static_cast<int>(candidates[randIdx]);
  util::AnkiStore::getInstance().incrementCardReadCount(static_cast<size_t>(selectedIndex));
  util::AnkiStore::getInstance().save();
  cards[static_cast<size_t>(selectedIndex)].readCount++;
}

void AnkiActivity::render(RenderLock&& lock) {
  if (mode == Mode::Hub) {
    renderHub(std::move(lock));
  } else if (mode == Mode::ReaderCards) {
    renderReaderCards(std::move(lock));
  } else {
    renderCsvDeck(std::move(lock));
  }
}

void AnkiActivity::renderHub(RenderLock&&) {
  renderer.clearScreen();

  const int totalEntries = static_cast<int>(1 + deckPaths.size());

  renderer.drawCenteredText(UI_12_FONT_ID, 40, tr(STR_ANKI_CARDS), true, EpdFontFamily::BOLD);

  // Draw menu entries
  const int startY = 100;
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID) + 8;
  for (int i = 0; i < totalEntries; ++i) {
    const int y = startY + i * lineH;
    const bool selected = (i == hubIndex);
    const char* label = (i == 0) ? tr(STR_ANKI_READER_CARDS) : deckTitles[static_cast<size_t>(i - 1)].c_str();
    if (selected) {
      // Draw selection indicator
      const int pageWidth = renderer.getScreenWidth();
      renderer.fillRect(20, y - 4, pageWidth - 40, lineH, true);
      renderer.drawCenteredText(UI_12_FONT_ID, y, label, false, EpdFontFamily::BOLD);
    } else {
      renderer.drawCenteredText(UI_12_FONT_ID, y, label);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_TOGGLE), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

void AnkiActivity::renderReaderCards(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 40, tr(STR_ANKI_CARDS), true, EpdFontFamily::BOLD);

  if (cards.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_ANKI_NO_CARDS));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 30, tr(STR_ANKI_ADD_HINT));
  } else {
    const auto& card = cards[static_cast<size_t>(selectedIndex)];

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

void AnkiActivity::renderCsvDeck(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_12_FONT_ID, 40, csvDeck.title.c_str(), true, EpdFontFamily::BOLD);

  if (csvQueue.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_ANKI_DECK_EMPTY));
  } else {
    const size_t cardIdx = csvQueue[csvQueuePos];
    const auto& card = csvDeck.cards[cardIdx];

    // Progress counter
    char progress[16];
    snprintf(progress, sizeof(progress), "%zu / %zu", csvQueuePos + 1, csvQueue.size());
    renderer.drawCenteredText(UI_10_FONT_ID, 70, progress);

    // Card Box
    const int boxMargin = 20;
    const int boxWidth = pageWidth - (boxMargin * 2);
    const int boxHeight = pageHeight - 200;
    const int boxY = 100;

    renderer.drawRect(boxMargin, boxY, boxWidth, boxHeight);

    if (csvShowingBack) {
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 30, tr(STR_ANKI_BACK), false, EpdFontFamily::ITALIC);
      std::vector<std::string> lines = renderer.wrappedText(
          UI_12_FONT_ID, card.back.empty() ? tr(STR_NONE_OPT) : card.back.c_str(), boxWidth - 40, 10);
      int y = boxY + 80;
      for (const auto& line : lines) {
        renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
        y += renderer.getLineHeight(UI_12_FONT_ID);
      }
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, boxY + 30, tr(STR_ANKI_FRONT), false, EpdFontFamily::ITALIC);
      renderer.drawCenteredText(UI_12_FONT_ID, boxY + boxHeight / 2 - 10, card.front.c_str(), true,
                                EpdFontFamily::BOLD);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_TOGGLE), tr(STR_ANKI_NEXT), tr(STR_ANKI_NEXT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

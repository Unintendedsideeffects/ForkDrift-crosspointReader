#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Activity.h"
#include "util/AnkiStore.h"
#include "util/ButtonNavigator.h"
#include "util/FlashcardsStore.h"

class AnkiActivity final : public Activity {
 public:
  explicit AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t {
    Hub,          // top-level menu: Reader Cards + CSV decks
    ReaderCards,  // existing reader-card review
    CsvDeck,      // CSV deck front/back review
  };

  // Hub state
  Mode mode = Mode::Hub;
  int hubIndex = 0;  // selected hub entry (0 = Reader Cards, 1+ = CSV decks)

  // Reader-cards review state (preserved exactly)
  int selectedIndex = 0;
  bool showingBack = false;
  ButtonNavigator buttonNavigator;
  std::vector<util::AnkiCard> cards;  // snapshot taken in onEnter()

  // CSV deck review state
  FlashcardDeck csvDeck;
  std::vector<FlashcardCardProgress> csvProgress;
  std::vector<size_t> csvQueue;  // indices into csvDeck.cards
  size_t csvQueuePos = 0;        // position within csvQueue
  bool csvShowingBack = false;

  // Hub menu entries: titles of CSV decks (empty = no decks found)
  std::vector<std::string> deckTitles;
  std::vector<std::string> deckPaths;

  void loadDecks();
  void enterCsvDeck(size_t deckIdx);
  void nextSrsCard();
  void renderHub(RenderLock&&);
  void renderReaderCards(RenderLock&&);
  void renderCsvDeck(RenderLock&&);
};

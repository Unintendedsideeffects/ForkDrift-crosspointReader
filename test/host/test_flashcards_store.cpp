#include "doctest/doctest.h"
#include "src/util/FlashcardsStore.h"
#include "test/mock/HalStorage.h"

TEST_CASE("flashcards parse basic and quoted CSV") {
  const auto cards = FlashcardsStore::parseCsvDeck("id,front,back\n1,\"Hello, world\",\"A \"\"quote\"\"\"\n2,Q2,A2\n");
  REQUIRE(cards.size() == 2);
  CHECK(cards[0].key == "1");
  CHECK(cards[0].front == "Hello, world");
  CHECK(cards[0].back == "A \"quote\"");
  CHECK(cards[1].front == "Q2");
}

TEST_CASE("flashcards load decks and progress from storage") {
  Storage.reset();
  Storage.mkdir("/decks");
  REQUIRE(Storage.writeFile("/decks/vocab.csv", "front,back\ncat,gatto\n"));

  const auto decks = FlashcardsStore::listDecks("/decks");
  REQUIRE(decks.size() == 1);
  CHECK(decks[0] == "/decks/vocab.csv");

  FlashcardDeck deck;
  REQUIRE(FlashcardsStore::loadDeck("/decks/vocab.csv", deck));
  REQUIRE(deck.cards.size() == 1);

  std::vector<FlashcardCardProgress> progress;
  REQUIRE(FlashcardsStore::loadDeckProgress(deck, progress));
  REQUIRE(progress.size() == 1);
  FlashcardsStore::recordAnswer(progress[0], 4, 10);
  REQUIRE(FlashcardsStore::saveDeckProgress(deck, progress));

  std::vector<FlashcardCardProgress> reloaded;
  REQUIRE(FlashcardsStore::loadDeckProgress(deck, reloaded));
  REQUIRE(reloaded.size() == 1);
  CHECK(reloaded[0].attempts == 1);
  CHECK(reloaded[0].correctCount == 1);
  CHECK(reloaded[0].dueDayOrdinal == 11);
}

TEST_CASE("flashcards build queues and summaries") {
  FlashcardDeck deck;
  deck.cards = {{"a", "a", "a"}, {"b", "b", "b"}, {"c", "c", "c"}};
  std::vector<FlashcardCardProgress> progress = {{"a"}, {"b"}, {"c"}};
  progress[1].attempts = 2;
  progress[1].failed = true;
  progress[2].attempts = 1;
  progress[2].dueDayOrdinal = 100;

  CHECK(FlashcardsStore::buildStudyQueue(deck, progress, FlashcardStudyMode::New, 10).size() == 1);
  CHECK(FlashcardsStore::buildStudyQueue(deck, progress, FlashcardStudyMode::Failed, 10).size() == 1);
  CHECK(FlashcardsStore::buildStudyQueue(deck, progress, FlashcardStudyMode::Due, 10).size() == 2);

  FlashcardSessionSummary summary;
  FlashcardsStore::updateSessionSummary(summary, true, true);
  FlashcardsStore::updateSessionSummary(summary, false, false);
  CHECK(summary.totalReviewed == 2);
  CHECK(summary.correctCount == 1);
  CHECK(summary.failedCount == 1);
  CHECK(summary.newSeenCount == 1);
}

#include <algorithm>

#include "src/util/PathUtils.h"

static bool isValidDeckPath(const std::string& path) {
  if (!PathUtils::isValidSdPath(path.c_str())) {
    return false;
  }
  bool startsWithFlashcards = (path.size() >= 12 && path.compare(0, 12, "/flashcards/") == 0);
  bool startsWithDecks = (path.size() >= 7 && path.compare(0, 7, "/decks/") == 0);
  if (!startsWithFlashcards && !startsWithDecks) {
    return false;
  }
  if (path.length() < 4) {
    return false;
  }
  std::string suffix = path.substr(path.length() - 4);
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
  if (suffix != ".csv") {
    return false;
  }
  return true;
}

TEST_CASE("flashcards deck path validation") {
  CHECK(isValidDeckPath("/flashcards/words.csv") == true);
  CHECK(isValidDeckPath("/decks/vocab.CSV") == true);
  CHECK(isValidDeckPath("/decks/sub/dir.csv") == true);
  CHECK(isValidDeckPath("/flashcards/../decks/words.csv") == false);
  CHECK(isValidDeckPath("/other/words.csv") == false);
  CHECK(isValidDeckPath("/decks/words.txt") == false);
  CHECK(isValidDeckPath("/decks/words.csv\\") == false);
  CHECK(isValidDeckPath("") == false);
}

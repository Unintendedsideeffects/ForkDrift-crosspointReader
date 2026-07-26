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

TEST_CASE("flashcards streaming parser preserves quoted newlines and CRLF") {
  std::vector<FlashcardCard> cards;
  const auto status =
      FlashcardsStore::parseCsvDeckBounded("card_id,question,answer\r\n1,\"line one\nline two\",\"A, B\"\r\n", cards);
  REQUIRE(status == FlashcardLoadStatus::Ok);
  REQUIRE(cards.size() == 1);
  CHECK(cards[0].key == "1");
  CHECK(cards[0].front == "line one\nline two");
  CHECK(cards[0].back == "A, B");
}

TEST_CASE("flashcards parser rejects malformed and bounded inputs without partial output") {
  std::vector<FlashcardCard> cards = {{"keep", "old", "value"}};
  CHECK(FlashcardsStore::parseCsvDeckBounded("front,back\n\"unterminated,value", cards) ==
        FlashcardLoadStatus::Malformed);
  REQUIRE(cards.size() == 1);
  CHECK(cards[0].key == "keep");

  const std::string longField(FlashcardsStore::MAX_FIELD_BYTES + 1, 'x');
  CHECK(FlashcardsStore::parseCsvDeckBounded("front,back\n" + longField + ",answer\n", cards) ==
        FlashcardLoadStatus::FieldTooLarge);
  CHECK(cards[0].key == "keep");
}

TEST_CASE("flashcards oversized production deck leaves caller deck unchanged") {
  Storage.reset();
  Storage.mkdir("/decks");
  const std::string oversized(FlashcardsStore::MAX_DECK_BYTES + 1, 'x');
  REQUIRE(Storage.writeFile("/decks/huge.csv", oversized.c_str()));
  FlashcardDeck deck;
  deck.title = "keep";
  std::string error;
  CHECK_FALSE(FlashcardsStore::loadDeck("/decks/huge.csv", deck, &error));
  CHECK(deck.title == "keep");
  CHECK(error.find("256 KiB") != std::string::npos);
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

TEST_CASE("flashcards deck path validation") {
  CHECK(FlashcardsStore::isValidDeckPath("/flashcards/words.csv") == true);
  CHECK(FlashcardsStore::isValidDeckPath("/decks/vocab.CSV") == true);
  CHECK(FlashcardsStore::isValidDeckPath("/decks/sub/dir.csv") == true);
  CHECK(FlashcardsStore::isValidDeckPath("/flashcards/../decks/words.csv") == false);
  CHECK(FlashcardsStore::isValidDeckPath("/other/words.csv") == false);
  CHECK(FlashcardsStore::isValidDeckPath("/decks/words.txt") == false);
  CHECK(FlashcardsStore::isValidDeckPath("/decks/words.csv\\") == false);
  CHECK(FlashcardsStore::isValidDeckPath("") == false);
}

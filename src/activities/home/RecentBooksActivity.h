#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/BookProgressDataStore.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

class RecentBooksActivity final : public Activity {
 private:
  struct PartyBookEntry {
    RecentBook book;
    bool hasProgress = false;
    BookProgressDataStore::ProgressData progress;
    int level = 1;
    std::string progressLabel;
    // Active-stage sprite resolved exactly like the party home screen
    // (PokemonProgress::activeSpeciesId at the progress level), so both
    // screens always show the same Pokémon.
    std::string spritePath;
    std::string pokemonLabel;
  };

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;
  bool partyMode = false;

  // Set when a long-press has fired; input is swallowed until Confirm is released
  // again so the release doesn't also open the book.
  bool longPressFired = false;

  // Recent tab state
  std::vector<PartyBookEntry> recentBooks;

  // Data loading
  void loadRecentBooks();
  bool drawCoverAt(const std::string& coverPath, int x, int y, int width, int height) const;
  // Draws a cached 1-bit Pokémon sprite (white = transparent) fitted into a
  // square box. Returns false when missing/unparseable.
  bool drawSpriteAt(const std::string& spritePath, int x, int y, int size) const;

  // Show an OK/Cancel prompt to remove the given book from the Recent Books list.
  void promptRemoveBook(const std::string& path, const std::string& title);

 public:
  explicit RecentBooksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RecentBooks", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

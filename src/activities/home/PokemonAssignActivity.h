#pragma once

#include <FeatureFlags.h>

#if ENABLE_POKEMON_PARTY

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "activities/Activity.h"

// Fully-offline on-device assignment of a prebaked team Pokémon to a recent
// book. The team (and every evolution stage's sprite) is built + cached over the
// web plugin; this activity needs no network. It chains two ListPickerActivity
// steps — pick a book, then pick a team member — and writes the chosen member's
// already-baked assignment object into the book's pokemon.json.
class PokemonAssignActivity final : public Activity {
 public:
  PokemonAssignActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PokemonAssign", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Phase { LaunchBookPicker, AwaitingPicker, Done, NoTeam };

  struct BookRef {
    std::string path;
    std::string title;
  };

  void launchBookPicker();
  void launchPokemonPicker();
  void writeAssignment(int memberIndex);

  Phase phase_ = Phase::LaunchBookPicker;
  JsonDocument teamDoc_;                // { "team": [ <member>, ... ] }, kept for writing the chosen member
  std::vector<std::string> teamNames_;  // display names parallel to teamDoc_["team"]
  std::vector<BookRef> books_;          // up to 6 most-recent books (the party)
  int chosenBookIndex_ = -1;
  std::string statusMessage_;
};

#endif  // ENABLE_POKEMON_PARTY

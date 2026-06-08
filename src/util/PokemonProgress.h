#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Per-book Pokémon assignment + reading-progress-as-level logic.
//
// A book's reading progress IS the Pokémon's level: 0% read => Lv 1, 100% read
// => Lv 100. As the level crosses each evolution stage's minimum level the
// active stage (and therefore the rendered sprite) advances. Assignment data is
// authored over the web plugin and stored as pokemon.json in the book cache
// (see PokemonBookDataStore); this helper is the read/compute side used by the
// Pokémon Party theme and the sleep screen.
struct PokemonEvolutionStage {
  int speciesId = 0;
  std::string name;
  int minLevel = 1;  // base stage is always reachable at level 1
};

struct PokemonAssignment {
  bool valid = false;
  int id = 0;                                // assigned Pokémon id (base form as picked on the web)
  int speciesId = 0;                         // base species id
  std::string name;                          // display / species name of the assignment
  std::vector<PokemonEvolutionStage> chain;  // ordered base -> final
};

namespace PokemonProgress {

// Highest level any book can reach.
constexpr int kMaxLevel = 100;

// Load + parse pokemon.json for a book. Returns a struct with valid=false when
// the book has no assignment (or the file is missing/corrupt).
PokemonAssignment loadForBook(const std::string& bookPath);

// Map a reading percentage (0..100; negative => unknown/unstarted) to a level
// in [1, kMaxLevel].
int levelForPercent(float percent);

// Index into assignment.chain of the active evolution stage for the given
// level (the last stage whose minLevel <= level). Returns 0 when the chain is
// empty or only the base form qualifies.
int activeStageIndex(const PokemonAssignment& assignment, int level);

// Species id that should be rendered for the given level. Falls back to the
// assignment's base speciesId / id when the chain is empty.
int activeSpeciesId(const PokemonAssignment& assignment, int level);

}  // namespace PokemonProgress

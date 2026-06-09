#include "util/PokemonProgress.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>

#include "util/PokemonBookDataStore.h"
#include "util/PokemonTeamStore.h"
#include "util/RecentBooksStore.h"

namespace {
PokemonAssignment assignmentFromPokemonJson(JsonVariantConst pokemon) {
  PokemonAssignment out;
  if (!pokemon.is<JsonObjectConst>()) {
    return out;
  }

  out.id = pokemon["id"] | 0;
  out.speciesId = pokemon["speciesId"] | out.id;
  const char* name = pokemon["speciesName"] | (pokemon["name"] | "");
  out.name = name ? name : "";

  JsonArrayConst evolution = pokemon["evolutionChain"].as<JsonArrayConst>();
  if (!evolution.isNull()) {
    out.chain.reserve(evolution.size());
    for (JsonVariantConst stage : evolution) {
      PokemonEvolutionStage entry;
      entry.speciesId = stage["speciesId"] | 0;
      const char* stageName = stage["name"] | "";
      entry.name = stageName ? stageName : "";
      const int minLevel = stage["minLevel"] | 0;
      entry.minLevel = minLevel > 0 ? minLevel : 1;
      if (entry.speciesId > 0) {
        out.chain.push_back(std::move(entry));
      }
    }
  }

  out.valid = out.speciesId > 0 || !out.chain.empty();
  if (out.speciesId <= 0 && !out.chain.empty()) {
    out.speciesId = out.chain.front().speciesId;
  }
  return out;
}
}  // namespace

namespace PokemonProgress {

PokemonAssignment loadForBook(const std::string& bookPath) {
  PokemonAssignment out;
  if (!PokemonBookDataStore::supportsBookPath(bookPath)) {
    return out;
  }

  JsonDocument doc;
  if (PokemonBookDataStore::loadPokemonDocument(bookPath, doc)) {
    return assignmentFromPokemonJson(doc["pokemon"]);
  }

  JsonDocument teamDoc;
  if (!PokemonTeamStore::loadTeamDocument(teamDoc) || !teamDoc["team"].is<JsonArrayConst>()) {
    return out;
  }
  const JsonArrayConst team = teamDoc["team"].as<JsonArrayConst>();
  const auto& recent = RECENT_BOOKS.getBooks();
  for (size_t i = 0; i < recent.size(); ++i) {
    if (recent[i].path != bookPath) {
      continue;
    }
    if (i < team.size()) {
      return assignmentFromPokemonJson(team[i]);
    }
    break;
  }
  return out;
}

int levelForPercent(float percent) {
  if (percent <= 0.0f || std::isnan(percent)) {
    return 1;
  }
  if (percent >= 100.0f) {
    return kMaxLevel;
  }
  // 0% -> Lv1, 100% -> Lv100, linear in between.
  const int level = 1 + static_cast<int>(std::lround(percent / 100.0f * (kMaxLevel - 1)));
  return std::clamp(level, 1, kMaxLevel);
}

int activeStageIndex(const PokemonAssignment& assignment, int level) {
  if (assignment.chain.empty()) {
    return 0;
  }
  int best = 0;
  for (size_t i = 0; i < assignment.chain.size(); ++i) {
    if (assignment.chain[i].minLevel <= level) {
      best = static_cast<int>(i);
    }
  }
  return best;
}

int activeSpeciesId(const PokemonAssignment& assignment, int level) {
  if (!assignment.chain.empty()) {
    return assignment.chain[static_cast<size_t>(activeStageIndex(assignment, level))].speciesId;
  }
  return assignment.speciesId > 0 ? assignment.speciesId : assignment.id;
}

}  // namespace PokemonProgress

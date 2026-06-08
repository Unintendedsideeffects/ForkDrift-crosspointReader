#pragma once

#include <ArduinoJson.h>

// Persistence for the prebaked Pokémon "team" roster.
//
// The team is built in the browser (the web plugin), which downloads each
// member's data + every evolution stage's sprite from PokéAPI, converts the
// sprites to 1-bit BMPs (see PokemonSpriteCache), and uploads the lot. The
// roster itself — one prebaked assignment object per team member — is stored
// here as a single JSON file so that assigning a team member to a book later
// (in the browser *or* on-device) needs no network at all.
//
// Shape: { "team": [ <pokemon-object>, ... ] } where each <pokemon-object> is
// the same structure written into a book's pokemon.json (id, speciesId, name,
// types, evolutionChain, sprites, ...). The device treats entries opaquely
// except for the fields the on-device assign menu reads (name, speciesId,
// evolutionChain) — the browser owns the schema.
namespace PokemonTeamStore {

// Absolute SD path of the team roster file (whether or not it exists yet).
const char* teamFilePath();

// Load the roster document. Returns false when the file is missing/corrupt.
bool loadTeamDocument(JsonDocument& doc);

// Persist the roster. `teamData` is stored verbatim under the top-level "team"
// key. Returns false on serialize/write failure.
bool saveTeamDocument(JsonVariantConst teamData);

}  // namespace PokemonTeamStore

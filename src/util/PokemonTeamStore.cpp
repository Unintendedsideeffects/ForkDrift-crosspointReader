#include "util/PokemonTeamStore.h"

#include <FsFileJsonReader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

namespace {
constexpr char kPokemonDir[] = "/.crosspoint/pokemon";
constexpr char kTeamFile[] = "/.crosspoint/pokemon/team.json";
}  // namespace

namespace PokemonTeamStore {

const char* teamFilePath() { return kTeamFile; }

bool loadTeamDocument(JsonDocument& doc) {
  if (!Storage.exists(kTeamFile)) {
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("PKM", kTeamFile, file)) {
    return false;
  }
  FsFileJsonReader reader(file);
  const DeserializationError error = deserializeJson(doc, reader);
  file.close();
  return !error;
}

bool saveTeamDocument(JsonVariantConst teamData) {
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(kPokemonDir);

  JsonDocument doc;
  doc["team"].set(teamData);

  const size_t jsonSize = measureJson(doc);
  auto jsonBuffer = makeUniqueNoThrow<char[]>(jsonSize + 1);
  if (!jsonBuffer) {
    LOG_ERR("PKM", "OOM: %d bytes for team JSON buffer", (int)(jsonSize + 1));
    return false;
  }
  serializeJson(doc, jsonBuffer.get(), jsonSize + 1);
  return Storage.writeFile(kTeamFile, jsonBuffer.get());
}

}  // namespace PokemonTeamStore

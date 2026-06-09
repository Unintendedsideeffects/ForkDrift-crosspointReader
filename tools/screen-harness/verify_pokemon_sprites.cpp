#include <Bitmap.h>
#include <HalStorage.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "device_fs_data.h"
#include "util/PokemonPartySprites.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"

namespace {
struct SpriteProbeResult {
  std::string bookPath;
  std::string title;
  bool assignmentValid = false;
  int speciesId = 0;
  std::string spritePath;
  bool fileExists = false;
  bool opened = false;
  BmpReaderError parseError = BmpReaderError::FileInvalid;
  bool is1Bit = false;
  int width = 0;
  int height = 0;
  bool drewSprite = false;
};

bool probeSpriteLoad(const std::string& path, SpriteProbeResult& out) {
  out.spritePath = path;
  out.fileExists = !path.empty() && Storage.exists(path.c_str());
  if (!out.fileExists) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("PKM", path, file)) {
    return false;
  }
  out.opened = true;

  Bitmap bitmap(file);
  out.parseError = bitmap.parseHeaders();
  out.is1Bit = bitmap.is1Bit();
  out.width = bitmap.getWidth();
  out.height = bitmap.getHeight();
  file.close();

  out.drewSprite = out.parseError == BmpReaderError::Ok && out.is1Bit;
  return out.drewSprite;
}

void printResult(const SpriteProbeResult& result) {
  std::cout << "book: " << result.title << '\n';
  std::cout << "  path: " << result.bookPath << '\n';
  std::cout << "  assignment: " << (result.assignmentValid ? "valid" : "missing") << '\n';
  std::cout << "  speciesId: " << result.speciesId << '\n';
  std::cout << "  sprite: " << result.spritePath << '\n';
  std::cout << "  exists: " << (result.fileExists ? "yes" : "no") << '\n';
  std::cout << "  opened: " << (result.opened ? "yes" : "no") << '\n';
  std::cout << "  parse: " << Bitmap::errorToString(result.parseError) << '\n';
  std::cout << "  is1Bit: " << (result.is1Bit ? "yes" : "no") << '\n';
  std::cout << "  size: " << result.width << 'x' << result.height << '\n';
  std::cout << "  drawSpriteInBox: " << (result.drewSprite ? "OK" : "FAIL") << '\n';
}

bool verifyRefreshPolicyWithMissingSprites(const PokemonPartySprites::SyncResult& sync) {
  PokemonPartySprites::RefreshState state;
  int refreshCount = 0;
  PokemonPartySprites::SyncResult stable = sync;

  for (int attempt = 0; attempt < PokemonPartySprites::kMaxRefreshRetries + 2; ++attempt) {
    const PokemonPartySprites::RefreshDecision decision = PokemonPartySprites::decideRefresh(state, stable);
    if (decision.forceFullRefresh) {
      ++refreshCount;
    }
  }

  const bool ok = refreshCount == PokemonPartySprites::kMaxRefreshRetries;
  std::cout << "refresh policy: " << refreshCount << " forced refreshes before stop (expected "
            << PokemonPartySprites::kMaxRefreshRetries << ")\n";
  return ok;
}
}  // namespace

int main() {
  std::filesystem::path deviceFsRoot;
  if (!mountDeviceFilesystem(deviceFsRoot)) {
    std::cerr << "verify_pokemon_sprites: no deviceFilesystem mount (set SCREEN_HARNESS_SD_ROOT)\n";
    return 1;
  }

  if (!RECENT_BOOKS.loadFromFile()) {
    std::cerr << "verify_pokemon_sprites: failed to load recent.json from snapshot\n";
    return 1;
  }

  const std::vector<RecentBook>& loadedBooks = RECENT_BOOKS.getBooks();
  if (loadedBooks.empty()) {
    std::cerr << "verify_pokemon_sprites: no recent books in snapshot\n";
    return 1;
  }

  const PokemonPartySprites::SyncResult sync = PokemonPartySprites::syncPartySprites(loadedBooks);
  std::cout << "sync: missing=" << sync.missingCount << " newlyCached=" << sync.newlyCachedCount
            << " fingerprint=" << sync.cacheFingerprint << '\n';

  int okCount = 0;
  int failCount = 0;
  for (const RecentBook& book : loadedBooks) {
    SpriteProbeResult result;
    result.bookPath = book.path;
    result.title = book.title.empty() ? book.path : book.title;

    const PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);
    result.assignmentValid = assignment.valid;

    const int level = PokemonProgress::levelForPercent(57.0f);
    result.speciesId = assignment.valid ? PokemonProgress::activeSpeciesId(assignment, level) : 0;

    const std::string path = PokemonSpriteCache::spritePath(result.speciesId);
    if (probeSpriteLoad(path, result)) {
      ++okCount;
    } else {
      ++failCount;
    }
    printResult(result);
    std::cout << '\n';
  }

  std::cout << "summary: " << okCount << " sprite loads OK, " << failCount << " failed\n";

  bool policyOk = true;
  if (sync.missingCount > 0) {
    policyOk = verifyRefreshPolicyWithMissingSprites(sync);
  } else {
    std::cout << "refresh policy: skipped (all party sprites cached)\n";
  }

  if (failCount != 0 || !policyOk) {
    return 1;
  }
  return 0;
}

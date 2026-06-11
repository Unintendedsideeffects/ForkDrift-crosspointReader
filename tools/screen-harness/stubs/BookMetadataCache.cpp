// Harness stub: PokemonPartyTheme only needs ctor (inline), load(), and
// getSpineCount() (inline). The real loader pulls ZipFile/Serialization which
// the harness doesn't link; a fixed chapter count is enough to verify layout.
#include <Epub/BookMetadataCache.h>

bool BookMetadataCache::load() {
  spineCount = 12;
  loaded = true;
  return true;
}

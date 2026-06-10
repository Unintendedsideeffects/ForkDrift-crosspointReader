#include "util/PokemonPartySprites.h"

#include <algorithm>

#include "util/BookProgressDataStore.h"
#include "util/PokemonProgress.h"
#include "util/PokemonSpriteCache.h"
#include "util/RecentBooksStore.h"

namespace PokemonPartySprites {
namespace {
constexpr int kPartyMaxBooks = 6;

uint32_t mixFingerprint(uint32_t hash, const int speciesId, const bool cached) {
  hash ^= static_cast<uint32_t>(speciesId);
  hash *= 16777619u;
  hash ^= cached ? 0xA5A5A5A5u : 0x5A5A5A5Au;
  hash *= 16777619u;
  return hash;
}
}  // namespace

SyncResult syncPartySprites(const std::vector<RecentBook>& recentBooks) {
  SyncResult result;
  result.cacheFingerprint = 2166136261u;

  const int bookCount = std::min(static_cast<int>(recentBooks.size()), kPartyMaxBooks);
  for (int i = 0; i < bookCount; ++i) {
    const RecentBook& book = recentBooks[static_cast<size_t>(i)];
    const PokemonAssignment assignment = PokemonProgress::loadForBook(book.path);
    if (!assignment.valid) {
      continue;
    }

    BookProgressDataStore::ProgressData progress{};
    const float percent = BookProgressDataStore::loadProgress(book.path, progress) ? progress.percent : 0.0f;
    const int level = PokemonProgress::levelForPercent(percent);
    const int speciesId = PokemonProgress::activeSpeciesId(assignment, level);
    if (speciesId <= 0) {
      continue;
    }

    const bool wasCached = PokemonSpriteCache::isCached(speciesId);
    if (!wasCached) {
      result.missingCount++;
      if (PokemonSpriteCache::ensureSpriteById(speciesId, PokemonSpriteCache::kDefaultSpriteSize,
                                               PokemonSpriteCache::kDefaultSpriteSize)) {
        result.newlyCachedCount++;
      }
    }

    result.cacheFingerprint =
        mixFingerprint(result.cacheFingerprint, speciesId, PokemonSpriteCache::isCached(speciesId));
  }

  return result;
}

RefreshDecision decideRefresh(RefreshState& state, const SyncResult& sync) {
  RefreshDecision decision;
  const uint32_t fingerprintBefore = state.cacheFingerprint;
  state.cacheFingerprint = sync.cacheFingerprint;

  if (sync.newlyCachedCount > 0) {
    state.retries = 0;
    decision.forceFullRefresh = true;
    decision.requestRedraw = true;
    return decision;
  }

  if (fingerprintBefore != 0 && sync.cacheFingerprint != fingerprintBefore) {
    state.retries = 0;
    decision.forceFullRefresh = true;
    decision.requestRedraw = true;
    return decision;
  }

  if (sync.missingCount > 0 && state.retries < kMaxRefreshRetries) {
    state.retries++;
    decision.forceFullRefresh = false;
    decision.requestRedraw = true;
  }

  return decision;
}

}  // namespace PokemonPartySprites

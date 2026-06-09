#pragma once

#include <cstdint>
#include <vector>

struct RecentBook;

namespace PokemonPartySprites {

constexpr int kMaxRefreshRetries = 3;

struct SyncResult {
  int missingCount = 0;
  int newlyCachedCount = 0;
  uint32_t cacheFingerprint = 0;
};

struct RefreshState {
  int retries = 0;
  uint32_t cacheFingerprint = 0;
};

struct RefreshDecision {
  bool forceFullRefresh = false;
  bool requestRedraw = false;
};

SyncResult syncPartySprites(const std::vector<RecentBook>& recentBooks);
RefreshDecision decideRefresh(RefreshState& state, const SyncResult& sync);

}  // namespace PokemonPartySprites

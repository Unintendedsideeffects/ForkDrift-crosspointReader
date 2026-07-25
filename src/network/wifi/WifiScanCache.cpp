#include "network/wifi/WifiScanCache.h"

#include <Arduino.h>

#include "util/WifiCredentialStore.h"

std::vector<WifiNetworkInfo> WifiScanCache::cached;
unsigned long WifiScanCache::storedAtMs = 0;
bool WifiScanCache::hasResult = false;

void WifiScanCache::store(std::vector<WifiNetworkInfo> networks) {
  cached = std::move(networks);
  storedAtMs = millis();
  hasResult = true;
}

bool WifiScanCache::isFresh() {
  if (!hasResult) {
    return false;
  }
  if (millis() - storedAtMs >= TTL_MS) {
    invalidate();
    return false;
  }
  return !cached.empty();
}

const std::vector<WifiNetworkInfo>& WifiScanCache::entries() { return cached; }

void WifiScanCache::invalidate() {
  // swap-with-empty rather than clear(): clear() keeps the capacity allocated,
  // which would pin ~1 KB of DRAM for the rest of the session.
  std::vector<WifiNetworkInfo>().swap(cached);
  hasResult = false;
  storedAtMs = 0;
}

void WifiScanCache::refreshSavedFlags() {
  for (auto& network : cached) {
    network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
  }
}

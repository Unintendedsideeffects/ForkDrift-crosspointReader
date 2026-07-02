#ifdef SIMULATOR
// Link stubs for fork APIs whose real implementations live in files the
// simulator build excludes (network/server, network/ota). Inert but safe.
#include <Logging.h>

#include "network/ota/OtaUpdater.h"

bool OtaUpdater::loadFeatureStoreCatalog() {
  LOG_DBG("OTA", "[SIM] feature store catalog not available");
  return false;
}
bool OtaUpdater::hasFeatureStoreCatalog() const { return false; }
const std::vector<OtaUpdater::FeatureStoreEntry>& OtaUpdater::getFeatureStoreEntries() const {
  static const std::vector<FeatureStoreEntry> kEmpty;
  return kEmpty;
}
bool OtaUpdater::selectFeatureStoreBundleByIndex(size_t) { return false; }
const String& OtaUpdater::getLastError() const {
  static const String kNone = "not supported in simulator";
  return kNone;
}

#include "network/server/RemoteKeyboardNetworkSession.h"

// Remote keyboard over WiFi is a no-op in the simulator (physical keyboard
// input goes through the SDL window instead).
RemoteKeyboardNetworkSession::~RemoteKeyboardNetworkSession() = default;
bool RemoteKeyboardNetworkSession::begin() { return false; }
void RemoteKeyboardNetworkSession::loop() {}
void RemoteKeyboardNetworkSession::end() {}
#endif

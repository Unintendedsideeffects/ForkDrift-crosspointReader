// ForkDrift-specific link stubs for the simulator build.
// These provide definitions for symbols that exist in ForkDrift but are not
// present in the upstream crosspoint-simulator library stubs.
#ifdef SIMULATOR

#include "network/background/BackgroundWifiService.h"
#include "network/server/CrossPointWebServer.h"

namespace features::web_wifi_setup {
void registerFeature() {}
}  // namespace features::web_wifi_setup

// BackgroundWifiService is excluded from the simulator build
// (network/BackgroundWifiService.cpp is in build_src_filter exclusions).
// Provide the static instance + no-op method stubs so callers link cleanly.
BackgroundWifiService BackgroundWifiService::instance;

bool BackgroundWifiService::start(const char*, const char*) { return false; }
bool BackgroundWifiService::startUsingCurrentConnection() { return false; }
void BackgroundWifiService::stop(bool) {}
bool BackgroundWifiService::startRetryActive() const { return false; }

// ForkDrift additions to CrossPointWebServer not in the simulator library.
void CrossPointWebServer::noteWebUiAccess() const {}
void CrossPointWebServer::setApRedirectPath(std::string) {}

// arduino_esp_crt_bundle_attach is referenced by KOReaderSyncClient but only
// available on-device via the Arduino-ESP32 TLS bundle.
extern "C" int arduino_esp_crt_bundle_attach(void*) { return 0; }

#endif  // SIMULATOR

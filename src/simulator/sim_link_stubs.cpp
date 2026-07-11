#ifdef SIMULATOR
// Link stubs for remote-keyboard session APIs whose real implementation lives
// in files the simulator build excludes (network/server). Inert but safe.
#include <Logging.h>

#include "network/server/RemoteKeyboardNetworkSession.h"

// Remote keyboard over WiFi is a no-op in the simulator (physical keyboard
// input goes through the SDL window instead).
RemoteKeyboardNetworkSession::~RemoteKeyboardNetworkSession() = default;
bool RemoteKeyboardNetworkSession::begin() { return false; }
void RemoteKeyboardNetworkSession::loop() {}
void RemoteKeyboardNetworkSession::end() {}
#endif

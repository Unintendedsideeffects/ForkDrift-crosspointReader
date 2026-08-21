// Host-test stubs for the two collaborators WifiCredentialStore notifies after
// a successful save. Neither is compiled into the host suite, and neither has
// any bearing on credential parsing, integrity or locking.
//
// CrossPointWebServer is defined here as an empty type purely so
// BackgroundWebServer's std::unique_ptr member has a complete type at the point
// its destructor is instantiated. No host translation unit includes the real
// definition, so there is nothing to conflict with.

#include "CrossPointState.h"
#include "network/background/BackgroundWebServer.h"

class CrossPointWebServer {};

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const { return true; }

BackgroundWebServer& BackgroundWebServer::getInstance() {
  static BackgroundWebServer instance;
  return instance;
}

void BackgroundWebServer::invalidateCredentialsCache() {}

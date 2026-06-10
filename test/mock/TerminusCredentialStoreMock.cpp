#include "util/TerminusCredentialStore.h"

TerminusCredentialStore& TerminusCredentialStore::getInstance() {
  static TerminusCredentialStore instance;
  return instance;
}

bool TerminusCredentialStore::load() { return false; }
bool TerminusCredentialStore::save() const { return false; }
void TerminusCredentialStore::clear() {
  apiKey_.clear();
  deviceId_.clear();
}
bool TerminusCredentialStore::hasCredentials() const { return !apiKey_.empty() && !deviceId_.empty(); }

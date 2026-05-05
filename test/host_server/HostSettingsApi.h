#pragma once
#include <string>

#include "HostWebServer.h"

namespace host {

// Mount /api/settings (GET+POST), /api/opds (GET+POST+delete),
// /api/plugins, and /api/status host-only routes.
void mountStubApiRoutes(HostWebServer& server, const std::string& settingsRoot);

}  // namespace host

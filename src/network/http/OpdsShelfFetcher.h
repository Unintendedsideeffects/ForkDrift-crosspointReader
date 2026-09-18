#pragma once

#include <string>
#include <vector>

#include "OpdsServerStore.h"
#include "network/http/FetchFailure.h"
#include "util/LibraryShelfStore.h"

namespace OpdsShelfFetcher {
http_fetch::Result fetchRootBooks(const OpdsServer& server, std::vector<LibraryShelfEntry>& entries);
}

#pragma once

#include <string>
#include <vector>

#include "OpdsServerStore.h"
#include "util/LibraryShelfStore.h"

namespace OpdsShelfFetcher {
bool fetchRootBooks(const OpdsServer& server, std::vector<LibraryShelfEntry>& entries);
}

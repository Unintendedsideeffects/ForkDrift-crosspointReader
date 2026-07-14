#pragma once

#include <ArduinoJson.h>
#include <WString.h>

#include "util/BookProgressDataStore.h"
#include "util/RecentBooksStore.h"

namespace network {

void appendBookProgressJson(JsonObject target, const BookProgressDataStore::ProgressData& progress);
String buildRecentBookJson(const RecentBook& book, bool includePokemon);

}  // namespace network

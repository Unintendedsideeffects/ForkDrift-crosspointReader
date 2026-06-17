#pragma once

#include <Arduino.h>

#include <vector>

#include "util/RecentBooksStore.h"

namespace network {

struct ReadingDataHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  String body = "Unhandled reading data request";

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

ReadingDataHttpResult buildRecentBooksResponse(const std::vector<RecentBook>& books, bool includePokemon);

ReadingDataHttpResult handleBookProgressRequest(const String& rawPath);

}  // namespace network

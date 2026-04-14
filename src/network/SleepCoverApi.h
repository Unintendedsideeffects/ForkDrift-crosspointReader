#pragma once

#include <Arduino.h>

#include <cstddef>
#include <functional>
#include <string>

namespace network {

struct SleepCoverHttpResult {
  int statusCode = 500;
  const char* contentType = "text/plain";
  String body = "Unhandled sleep cover request";

  bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

using SleepCoverResolver = std::function<bool(const String& bookPath, std::string& coverPath)>;
using SleepCoverSaveSettings = std::function<bool()>;

SleepCoverHttpResult buildSleepCoverGetResponse(const char* pinnedPath);

SleepCoverHttpResult handleSleepCoverPinRequest(bool hasBody, const String& body, char* pinnedPath,
                                                size_t pinnedPathSize, const SleepCoverResolver& resolveBookCoverPath,
                                                const SleepCoverSaveSettings& saveSettings);

}  // namespace network

#include "util/TerminusApi.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <limits>

#include "util/UrlUtils.h"

namespace terminus_api {
namespace {

bool parseRefreshInterval(const JsonVariantConst value, uint32_t& out) {
  if (value.isNull()) {
    out = terminus_refresh::kDefaultIntervalS;
    return true;
  }

  if (value.is<uint32_t>()) {
    out = value.as<uint32_t>();
    return true;
  }

  if (!value.is<const char*>()) {
    return false;
  }

  const char* text = value.as<const char*>();
  if (!text || text[0] == '\0' || text[0] == '-') {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  out = static_cast<uint32_t>(parsed);
  return true;
}

}  // namespace

bool isAllowedRemoteUrl(const std::string& url) {
  if (UrlUtils::isPrivateLanHttpUrl(url)) {
    return true;
  }
  if (!UrlUtils::isHttpsUrl(url)) {
    return false;
  }

  constexpr size_t kHttpsPrefixLength = 8;
  const size_t hostEnd = url.find_first_of("/:?#", kHttpsPrefixLength);
  const size_t hostLength = (hostEnd == std::string::npos ? url.size() : hostEnd) - kHttpsPrefixLength;
  if (hostLength == 0) {
    return false;
  }
  return std::none_of(url.begin(), url.end(), [](const unsigned char c) { return std::isspace(c) != 0; });
}

std::string normalizeBaseUrl(std::string url) {
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
  if (url.empty() || url == kLegacyDefaultBaseUrl) {
    return kDefaultBaseUrl;
  }
  return url;
}

bool parseDisplayManifest(const std::string& json, DisplayManifest& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    return false;
  }

  const char* imageUrl = doc["image_url"] | "";
  if (imageUrl[0] == '\0' || !isAllowedRemoteUrl(imageUrl)) {
    return false;
  }

  uint32_t requestedInterval = terminus_refresh::kDefaultIntervalS;
  if (!parseRefreshInterval(doc["refresh_rate"], requestedInterval)) {
    return false;
  }

  DisplayManifest parsed;
  parsed.imageUrl = imageUrl;
  parsed.refreshIntervalS = terminus_refresh::normalizeServerInterval(requestedInterval);
  out = std::move(parsed);
  return true;
}

}  // namespace terminus_api

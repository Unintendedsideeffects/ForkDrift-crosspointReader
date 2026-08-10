#pragma once

#include <cstdint>
#include <string>

#include "util/TerminusRefreshPolicy.h"

namespace terminus_api {

inline constexpr char kDefaultBaseUrl[] = "https://trmnl.com";
inline constexpr char kLegacyDefaultBaseUrl[] = "https://api.trmnl.com";

struct DisplayManifest {
  std::string imageUrl;
  uint32_t refreshIntervalS = terminus_refresh::kDefaultIntervalS;
};

// HTTPS is allowed globally. Plain HTTP is limited to numeric private-LAN
// hosts for self-hosted Terminus/BYOS installations.
bool isAllowedRemoteUrl(const std::string& url);

// Canonicalize user/persisted base URLs and migrate the obsolete cloud host.
std::string normalizeBaseUrl(std::string url);

// Parse the subset of /api/display that the firmware consumes. TRMNL cloud
// currently serializes refresh_rate as a decimal string, while BYOS servers
// may use a JSON number, so both forms are accepted.
bool parseDisplayManifest(const std::string& json, DisplayManifest& out);

}  // namespace terminus_api

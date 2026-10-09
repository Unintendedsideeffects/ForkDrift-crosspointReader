#pragma once

#include <cstdint>
#include <string_view>

// Origin (scheme + host + port) comparison for HTTP redirect handling. Credentials
// are scoped to the origin they were configured for: a redirect may take the
// request elsewhere, but must not take the Authorization header with it.
// Absorbed in spirit from upstream d6f7f2565 (HttpRedirectPolicy); ours follows
// redirects through esp_http_client's HTTP_EVENT_REDIRECT instead of a manual loop.
namespace http_origin {

struct Origin {
  bool https = false;
  std::string_view host;  // views into the parsed URL; valid while it lives
  uint16_t port = 0;
};

inline bool equalsIgnoreCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    const char l = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
    const char r = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
    if (l != r) return false;
  }
  return true;
}

// Parses "scheme://[userinfo@]host[:port]/..." into its origin. Returns false for
// anything that is not an absolute http(s) URL with a host.
inline bool parse(const std::string_view url, Origin& out) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos) return false;
  const std::string_view scheme = url.substr(0, schemeEnd);
  if (equalsIgnoreCase(scheme, "https")) {
    out.https = true;
  } else if (equalsIgnoreCase(scheme, "http")) {
    out.https = false;
  } else {
    return false;
  }

  const size_t authStart = schemeEnd + 3;
  const size_t authEnd = url.find_first_of("/?#", authStart);
  std::string_view authority = url.substr(authStart, authEnd == std::string_view::npos ? authEnd : authEnd - authStart);
  const size_t at = authority.rfind('@');
  if (at != std::string_view::npos) authority.remove_prefix(at + 1);

  out.port = out.https ? 443 : 80;
  const size_t colon = authority.rfind(':');
  if (colon != std::string_view::npos && authority.find(']') == std::string_view::npos) {
    const std::string_view portText = authority.substr(colon + 1);
    if (portText.empty()) return false;
    uint32_t port = 0;
    for (const char c : portText) {
      if (c < '0' || c > '9') return false;
      port = port * 10 + static_cast<uint32_t>(c - '0');
      if (port > UINT16_MAX) return false;
    }
    if (port == 0) return false;
    out.port = static_cast<uint16_t>(port);
    authority = authority.substr(0, colon);
  }
  out.host = authority;
  return !out.host.empty();
}

inline bool sameOrigin(const Origin& a, const Origin& b) {
  return a.https == b.https && a.port == b.port && equalsIgnoreCase(a.host, b.host);
}

// True when following from -> to would leave TLS.
inline bool isDowngrade(const Origin& from, const Origin& to) { return from.https && !to.https; }

}  // namespace http_origin

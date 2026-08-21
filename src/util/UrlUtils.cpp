#include "UrlUtils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace UrlUtils {
namespace {
bool isHexDigit(const char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }

bool shouldEncode(const unsigned char c) {
  if (c <= 0x20 || c >= 0x7f) return true;
  switch (c) {
    case '"':
    case '<':
    case '>':
    case '\\':
    case '^':
    case '`':
    case '{':
    case '|':
    case '}':
      return true;
    default:
      return false;
  }
}
}  // namespace

bool isHttpsUrl(const std::string& url) { return url.size() >= 8 && url.compare(0, 8, "https://") == 0; }

bool isPrivateLanHttpUrl(const std::string& url) {
  static constexpr char kPrefix[] = "http://";
  static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
  if (url.compare(0, kPrefixLen, kPrefix) != 0) {
    return false;
  }
  const size_t hostEnd = url.find_first_of(":/", kPrefixLen);
  const std::string host =
      url.substr(kPrefixLen, hostEnd == std::string::npos ? std::string::npos : hostEnd - kPrefixLen);
  // Numeric IPv4 only — a hostname here could resolve to a public address.
  if (!std::all_of(host.begin(), host.end(), [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
    return false;
  }
  if (host.rfind("10.", 0) == 0 || host.rfind("192.168.", 0) == 0 || host.rfind("127.", 0) == 0) {
    return true;
  }
  if (host.rfind("172.", 0) == 0) {
    const int secondOctet = std::atoi(host.c_str() + 4);
    return secondOctet >= 16 && secondOctet <= 31;
  }
  return false;
}

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    // No protocol, find first slash
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  // Find the first slash after the protocol
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string encodeUnsafeUrlChars(const std::string& url) {
  std::string out;
  out.reserve(url.size());
  for (size_t i = 0; i < url.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(url[i]);
    if (c == '%' && i + 2 < url.size() && isHexDigit(url[i + 1]) && isHexDigit(url[i + 2])) {
      out += url[i];
      out += url[i + 1];
      out += url[i + 2];
      i += 2;
    } else if (c == '%' || shouldEncode(c)) {
      char encoded[4];
      snprintf(encoded, sizeof(encoded), "%%%02X", c);
      out += encoded;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  // If path is already an absolute URL (has protocol), use it directly
  if (path.find("://") != std::string::npos) {
    return encodeUnsafeUrlChars(path);
  }
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return encodeUnsafeUrlChars(urlWithProtocol);
  }
  if (path[0] == '/') {
    // Absolute path - use just the host
    return encodeUnsafeUrlChars(extractHost(urlWithProtocol) + path);
  }
  // Relative path - strip query string from base before appending
  std::string base = urlWithProtocol;
  const size_t queryPos = base.find('?');
  if (queryPos != std::string::npos) {
    base.resize(queryPos);
  }
  if (base.back() == '/') {
    return encodeUnsafeUrlChars(base + path);
  }
  return encodeUnsafeUrlChars(base + "/" + path);
}

}  // namespace UrlUtils

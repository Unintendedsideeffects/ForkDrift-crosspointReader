#include "UrlUtils.h"

#include <cstdlib>

namespace UrlUtils {

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
  for (const char c : host) {
    if ((c < '0' || c > '9') && c != '.') {
      return false;
    }
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

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  // If path is already an absolute URL (has protocol), use it directly
  if (path.find("://") != std::string::npos) {
    return path;
  }
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return urlWithProtocol;
  }
  if (path[0] == '/') {
    // Absolute path - use just the host
    return extractHost(urlWithProtocol) + path;
  }
  // Relative path - strip query string from base before appending
  std::string base = urlWithProtocol;
  const size_t queryPos = base.find('?');
  if (queryPos != std::string::npos) {
    base.resize(queryPos);
  }
  if (base.back() == '/') {
    return base + path;
  }
  return base + "/" + path;
}

}  // namespace UrlUtils

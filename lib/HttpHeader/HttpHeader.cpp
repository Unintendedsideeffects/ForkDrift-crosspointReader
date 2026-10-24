#include "HttpHeader.h"

#include <cstring>

namespace {

std::string_view trimAscii(std::string_view text) {
  while (!text.empty() &&
         (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
    text.remove_suffix(1);
  }
  return text;
}

bool equalsIgnoreCase(const std::string_view value, const char* expected) {
  const size_t expectedLen = std::strlen(expected);
  if (value.size() != expectedLen) {
    return false;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    char c = value[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != expected[i]) {
      return false;
    }
  }
  return true;
}

bool isTokenChar(const unsigned char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
    return true;
  }
  return std::strchr("!#$%&'*+-.^_`|~", static_cast<char>(c)) != nullptr;
}

}  // namespace

bool isValidHttpHeader(const std::string_view name, const std::string_view value) {
  if (name.empty()) {
    return value.empty();
  }
  if (name.size() + value.size() > 160) {
    return false;
  }
  for (const unsigned char c : name) {
    if (!isTokenChar(c)) {
      return false;
    }
  }
  for (const unsigned char c : value) {
    if (c < 32 || c == 127) {
      return false;
    }
  }
  static constexpr const char* kReserved[] = {"host",       "content-length", "transfer-encoding",
                                              "connection", "x-auth-user",    "x-auth-key",
                                              "accept",     "content-type",   "authorization"};
  for (const char* reserved : kReserved) {
    if (equalsIgnoreCase(name, reserved)) {
      return false;
    }
  }
  return true;
}

bool parseHttpHeaderLine(std::string_view line, HttpHeader& header) {
  line = trimAscii(line);
  const size_t colon = line.find(':');
  const std::string_view name = trimAscii(line.substr(0, colon));
  const std::string_view value =
      colon == std::string_view::npos ? std::string_view{} : trimAscii(line.substr(colon + 1));
  if (!isValidHttpHeader(name, value)) {
    return false;
  }
  header.name.assign(name.data(), name.size());
  header.value.assign(value.data(), value.size());
  return true;
}

std::string formatHttpHeaderLine(const HttpHeader& header, const bool maskValue) {
  if (header.name.empty()) {
    return {};
  }
  if (header.value.empty()) {
    return header.name;
  }
  return header.name + ": " + (maskValue ? "******" : header.value);
}

#pragma once

#include <string>
#include <string_view>

struct HttpHeader {
  std::string name;
  std::string value;
};

bool isValidHttpHeader(std::string_view name, std::string_view value);
bool parseHttpHeaderLine(std::string_view line, HttpHeader& header);
std::string formatHttpHeaderLine(const HttpHeader& header, bool maskValue = false);

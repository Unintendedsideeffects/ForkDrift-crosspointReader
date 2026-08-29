#include <fstream>
#include <sstream>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("health route is a fixed unauthenticated availability contract") {
  std::ifstream source("src/network/server/StaticHandlers.cpp");
  REQUIRE(source.good());

  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("{\"/health\", HTTP_GET, handleHealth, nullptr}") != std::string::npos);
  CHECK(text.find("server->send(200, \"application/json\", \"{\\\"status\\\":\\\"ok\\\"}\")") != std::string::npos);
}

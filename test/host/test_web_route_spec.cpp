#include <WebServer.h>

#include "core/registries/WebRouteRegistry.h"
#include "doctest/doctest.h"

TEST_CASE("webRouteSpecMatches requires method and exact uri") {
  void (*handler)(WebServer*) = [](WebServer*) {};
  const core::WebRouteSpec spec{"/api/status", HTTP_GET, handler, nullptr};

  CHECK(core::webRouteSpecMatches(spec, HTTP_GET, "/api/status"));
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_POST, "/api/status"));
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_GET, "/api/status/"));
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_GET, "/api/statu"));
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_GET, nullptr));
}

TEST_CASE("webRouteSpecMatches treats HTTP_ANY as a method wildcard") {
  void (*handler)(WebServer*) = [](WebServer*) {};
  const core::WebRouteSpec spec{"/files", HTTP_ANY, handler, nullptr};

  CHECK(core::webRouteSpecMatches(spec, HTTP_GET, "/files"));
  CHECK(core::webRouteSpecMatches(spec, HTTP_POST, "/files"));
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_GET, "/other"));
}

TEST_CASE("webRouteSpecMatches rejects a missing handler") {
  const core::WebRouteSpec spec{"/health", HTTP_GET, nullptr, nullptr};
  CHECK_FALSE(core::webRouteSpecMatches(spec, HTTP_GET, "/health"));
}

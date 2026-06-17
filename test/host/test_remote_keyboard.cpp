#include <WebServer.h>

#include <string>

#include "doctest/doctest.h"
#include "network/server/RemoteKeyboardSession.h"
#include "src/core/registries/WebRouteRegistry.h"
#include "src/features/remote_keyboard_input/Registration.h"

TEST_CASE("testRemoteKeyboardSessionAndRoutes") {
  features::remote_keyboard_input::registerFeature();
  CHECK(core::WebRouteRegistry::shouldRegister("remote_keyboard_input_api"));

  WebServer server;
  core::WebRouteRegistry::mountAll(&server);
  CHECK(server.hasRoute("/remote-input", HTTP_GET));
  CHECK(server.hasRoute("/api/remote-keyboard/session", HTTP_GET));
  CHECK(server.hasRoute("/api/remote-keyboard/claim", HTTP_POST));
  CHECK(server.hasRoute("/api/remote-keyboard/submit", HTTP_POST));

  server.setRequest(HTTP_GET, "/api/remote-keyboard/session");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);
  CHECK(server.response().body.indexOf("\"active\":false") != -1);

  const uint32_t sessionId = REMOTE_KEYBOARD_SESSION.begin("WiFi Password", "draft", 64, true);

  server.setRequest(HTTP_GET, "/api/remote-keyboard/session");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);
  CHECK(server.response().body.indexOf("\"active\":true") != -1);
  CHECK(server.response().body.indexOf("\"title\":\"WiFi Password\"") != -1);

  server.setRequest(HTTP_POST, "/api/remote-keyboard/claim");
  server.setBody(String("{\"id\":") + String(static_cast<int>(sessionId)) + ",\"client\":\"android\"}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);
  CHECK(server.response().body.indexOf("\"claimedBy\":\"android\"") != -1);

  server.setRequest(HTTP_POST, "/api/remote-keyboard/submit");
  server.setBody(String("{\"id\":") + String(static_cast<int>(sessionId)) + ",\"text\":\"secret\"}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);
  CHECK(server.response().body.indexOf("\"ok\":true") != -1);

  std::string submittedText;
  CHECK(REMOTE_KEYBOARD_SESSION.takeSubmitted(sessionId, submittedText));
  CHECK(submittedText == "secret");

  const uint32_t limitedSessionId = REMOTE_KEYBOARD_SESSION.begin("PIN", "", 4, false);
  server.setRequest(HTTP_POST, "/api/remote-keyboard/submit");
  server.setBody(String("{\"id\":") + String(static_cast<int>(limitedSessionId)) + ",\"text\":\"12345\"}");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 400);
  CHECK(server.response().body == "Text exceeds session length limit");
  REMOTE_KEYBOARD_SESSION.cancel(limitedSessionId);

  server.setRequest(HTTP_GET, "/remote-input");
  CHECK(server.dispatch());
  CHECK(server.response().statusCode == 200);
  CHECK(server.response().contentType == "text/html; charset=utf-8");
  const auto encodingHeader = server.response().headers.find("Content-Encoding");
  CHECK(encodingHeader != server.response().headers.end());
  CHECK(encodingHeader->second == "gzip");
}

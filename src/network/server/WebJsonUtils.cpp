#include "network/server/WebJsonUtils.h"

namespace network {

void sendJson(WebServer* server, const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

}  // namespace network

#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>

namespace network {

// Serialize `doc` and send it as a 200 application/json response.
void sendJson(WebServer* server, const JsonDocument& doc);

}  // namespace network

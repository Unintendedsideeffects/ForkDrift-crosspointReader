#pragma once

#include <DNSServer.h>

#include <memory>
#include <string>

#include "network/server/CrossPointWebServer.h"

class RemoteKeyboardNetworkSession {
 public:
  struct State {
    bool ready = false;
    bool apMode = false;
    bool usingExistingServer = false;
    std::string ssid;
    std::string ip;
    std::string url;
  };

  ~RemoteKeyboardNetworkSession();

  bool begin();
  void loop();
  void end();

  const State& snapshot() const { return state; }
  bool ownsServer() const { return ownedServer != nullptr; }

 private:
  bool startServerOnCurrentConnection();
  bool startAccessPointAndServer();

  std::unique_ptr<CrossPointWebServer> ownedServer;
  std::unique_ptr<DNSServer> dnsServer;
  State state;
  bool startedAp = false;
};

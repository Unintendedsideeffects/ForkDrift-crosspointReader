#include "network/server/RemoteKeyboardNetworkSession.h"

#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include "activities/ActivityManager.h"
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiService.h"
#include "network/server/CrossPointWebServer.h"
#include "network/wifi/WifiUtil.h"
#include "util/NetworkNames.h"

namespace {
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxConnections = 4;
constexpr uint16_t kDnsPort = 53;
}  // namespace

RemoteKeyboardNetworkSession::~RemoteKeyboardNetworkSession() = default;

bool RemoteKeyboardNetworkSession::begin() {
  end();

  if (hasStaWifiConnection()) {
    return startServerOnCurrentConnection();
  }

  return startAccessPointAndServer();
}

void RemoteKeyboardNetworkSession::loop() {
  if (dnsServer) {
    dnsServer->processNextRequest();
  }
  if (ownedServer && ownedServer->isRunning()) {
    ownedServer->handleClient();
  }
}

void RemoteKeyboardNetworkSession::end() {
  if (dnsServer) {
    dnsServer->stop();
    dnsServer.reset();
  }

  if (ownedServer) {
    ownedServer->stop();
    ownedServer.reset();
  }

  if (startedAp) {
    WiFi.softAPdisconnect(true);
    delay(30);
    WiFi.mode(WIFI_OFF);
    delay(30);
  }

  startedAp = false;
  state = {};
}

bool RemoteKeyboardNetworkSession::startServerOnCurrentConnection() {
  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }

  state.apMode = false;
  state.ssid = WiFi.SSID().c_str();
  state.ip = WiFi.localIP().toString().c_str();
  state.url = "http://" + state.ip + "/remote-input";

  if (BackgroundWebServer::getInstance().isRunning()) {
    state.ready = true;
    state.usingExistingServer = true;
    return true;
  }

  ownedServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!ownedServer) {
    LOG_ERR("RKS", "OOM: CrossPointWebServer");
    state = {};
    return false;
  }
  ownedServer->begin();
  if (!ownedServer->isRunning()) {
    ownedServer.reset();
    state = {};
    return false;
  }

  state.ready = true;
  return true;
}

bool RemoteKeyboardNetworkSession::startAccessPointAndServer() {
  if (BG_WIFI.isRunning()) {
    BG_WIFI.stop(false);
  }

  WiFi.mode(WIFI_AP);
  delay(100);

  const IPAddress apLocalIp(192, 168, 4, 1);
  const IPAddress apGateway(192, 168, 4, 1);
  const IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apLocalIp, apGateway, apSubnet);

  char apSsid[40];
  NetworkNames::getApSsid(apSsid, sizeof(apSsid));
  if (!WiFi.softAP(apSsid, nullptr, kApChannel, false, kApMaxConnections)) {
    WiFi.mode(WIFI_OFF);
    return false;
  }
  delay(100);

  startedAp = true;
  state.apMode = true;
  state.ssid = apSsid;
  state.ip = WiFi.softAPIP().toString().c_str();
  state.url = "http://" + state.ip + "/remote-input";

  dnsServer = makeUniqueNoThrow<DNSServer>();
  if (!dnsServer) {
    LOG_ERR("RKS", "OOM: DNSServer");
    end();
    return false;
  }
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(kDnsPort, "*", WiFi.softAPIP());

  ownedServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!ownedServer) {
    LOG_ERR("RKS", "OOM: CrossPointWebServer");
    end();
    return false;
  }
  ownedServer->setApRedirectPath("/remote-input");
  ownedServer->begin();
  if (!ownedServer->isRunning()) {
    end();
    return false;
  }

  state.ready = true;
  return true;
}

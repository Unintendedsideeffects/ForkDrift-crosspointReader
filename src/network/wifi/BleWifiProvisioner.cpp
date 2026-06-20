#include "network/wifi/BleWifiProvisioner.h"

#if ENABLE_BLE_WIFI_PROVISIONING

#include <Logging.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>

#include "network/wifi/BleCredentialParser.h"

namespace {
constexpr const char* kServiceUuid = "41cb0001-b8f4-4e4a-9f49-ecb9d6fd4b90";
constexpr const char* kCharacteristicUuid = "41cb0002-b8f4-4e4a-9f49-ecb9d6fd4b90";
}  // namespace

class BleWifiProvisioner::ServerDisconnectCallbacks : public NimBLEServerCallbacks {
 public:
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    (void)pServer;
    (void)connInfo;
    (void)reason;
    NimBLEDevice::startAdvertising();
    LOG_DBG("BLE", "Client disconnected — restarted advertising");
  }
};

class BleWifiProvisioner::CredentialCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit CredentialCharacteristicCallbacks(BleWifiProvisioner* owner) : owner(owner) {}

  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& connInfo) override {
    (void)connInfo;
    if (!owner || !characteristic) {
      return;
    }

    std::string payload = characteristic->getValue();
    owner->handleIncomingPayload(payload);
  }

 private:
  BleWifiProvisioner* owner;
};

BleWifiProvisioner::BleWifiProvisioner() : stateMutex(xSemaphoreCreateMutex()) {
  if (!stateMutex) {
    LOG_ERR("BLE", "Failed to create state mutex - BLE Wifi Provisioner init failed");
  }
}

BleWifiProvisioner::~BleWifiProvisioner() {
  stop();
  if (stateMutex) {
    vSemaphoreDelete(stateMutex);
    stateMutex = nullptr;
  }
}

bool BleWifiProvisioner::start(const std::string& deviceName) {
  if (running.load()) {
    return true;
  }
  if (!stateMutex) {
    return false;
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  credentialsReady = false;
  pendingSsid.clear();
  pendingPassword.clear();
  statusMessage = "Starting BLE...";
  xSemaphoreGive(stateMutex);

  NimBLEDevice::init(deviceName.c_str());
  server = NimBLEDevice::createServer();
  if (!server) {
    setStatusMessage("BLE server create failed");
    NimBLEDevice::deinit(true);
    return false;
  }

  serverCallbacks = new (std::nothrow) ServerDisconnectCallbacks();
  if (!serverCallbacks) {
    setStatusMessage("OOM: BLE server callbacks");
    NimBLEDevice::deinit(true);
    server = nullptr;
    return false;
  }
  server->setCallbacks(serverCallbacks);

  service = server->createService(kServiceUuid);
  if (!service) {
    setStatusMessage("BLE service create failed");
    NimBLEDevice::deinit(true);
    server = nullptr;
    return false;
  }

  characteristic = service->createCharacteristic(kCharacteristicUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  if (!characteristic) {
    setStatusMessage("BLE characteristic create failed");
    NimBLEDevice::deinit(true);
    server = nullptr;
    service = nullptr;
    return false;
  }

  callbacks = new (std::nothrow) CredentialCharacteristicCallbacks(this);
  if (!callbacks) {
    setStatusMessage("OOM: BLE characteristic callbacks");
    NimBLEDevice::deinit(true);
    server = nullptr;
    service = nullptr;
    return false;
  }
  characteristic->setCallbacks(callbacks);
  characteristic->setValue("Send WiFi credentials payload");

  service->start();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

  running.store(true);
  setStatusMessage("BLE ready: write SSID/password");
  LOG_DBG("BLE", "WiFi provisioning BLE started");
  return true;
}

void BleWifiProvisioner::stop() {
  if (!running.exchange(false)) {
    return;
  }

  NimBLEDevice::stopAdvertising();
  NimBLEDevice::deinit(true);

  if (callbacks) {
    delete callbacks;
    callbacks = nullptr;
  }
  if (serverCallbacks) {
    delete serverCallbacks;
    serverCallbacks = nullptr;
  }
  server = nullptr;
  service = nullptr;
  characteristic = nullptr;
  setStatusMessage("BLE stopped");
  LOG_DBG("BLE", "WiFi provisioning BLE stopped");
}

bool BleWifiProvisioner::takeCredentials(std::string& ssidOut, std::string& passwordOut) {
  if (!stateMutex) {
    return false;
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (!credentialsReady) {
    xSemaphoreGive(stateMutex);
    return false;
  }

  ssidOut = pendingSsid;
  passwordOut = pendingPassword;
  credentialsReady = false;
  pendingSsid.clear();
  pendingPassword.clear();
  xSemaphoreGive(stateMutex);
  return true;
}

std::string BleWifiProvisioner::getStatusMessage() const {
  if (!stateMutex) {
    return "BLE unavailable";
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  std::string message = statusMessage;
  xSemaphoreGive(stateMutex);
  return message;
}

void BleWifiProvisioner::handleIncomingPayload(const std::string& payload) {
  if (!stateMutex) {
    LOG_ERR("BLE", "State mutex is null, handleIncomingPayload aborted");
    return;
  }
  std::string ssid;
  std::string password;
  if (!ble_credential_parser::parse(payload, ssid, password)) {
    setStatusMessage("Invalid payload");
    return;
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  pendingSsid = ssid;
  pendingPassword = password;
  credentialsReady = true;
  statusMessage = "Credentials received";
  xSemaphoreGive(stateMutex);

  LOG_DBG("BLE", "Received WiFi credentials over BLE for SSID: %s", ssid.c_str());
}

void BleWifiProvisioner::setStatusMessage(const std::string& message) {
  if (!stateMutex) {
    return;
  }

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  statusMessage = message;
  xSemaphoreGive(stateMutex);
}

#endif

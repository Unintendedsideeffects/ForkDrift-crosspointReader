#include "BleWifiProvisioner.h"

#if ENABLE_BLE_WIFI_PROVISIONING

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <Logging.h>

#include "BleCredentialParser.h"

namespace {
constexpr const char* kServiceUuid = "41cb0001-b8f4-4e4a-9f49-ecb9d6fd4b90";
constexpr const char* kCharacteristicUuid = "41cb0002-b8f4-4e4a-9f49-ecb9d6fd4b90";
}  // namespace

class BleWifiProvisioner::ServerDisconnectCallbacks : public BLEServerCallbacks {
 public:
  void onDisconnect(BLEServer* pServer) override {
    (void)pServer;
    BLEDevice::startAdvertising();
    LOG_DBG("BLE", "Client disconnected — restarted advertising");
  }
};

class BleWifiProvisioner::CredentialCharacteristicCallbacks : public BLECharacteristicCallbacks {
 public:
  explicit CredentialCharacteristicCallbacks(BleWifiProvisioner* owner) : owner(owner) {}

  void onWrite(BLECharacteristic* characteristic) override {
    if (!owner || !characteristic) {
      return;
    }

    std::string payload = characteristic->getValue();
    owner->handleIncomingPayload(payload);
  }

 private:
  BleWifiProvisioner* owner;
};

BleWifiProvisioner::BleWifiProvisioner() : stateMutex(xSemaphoreCreateMutex()) {}

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

  BLEDevice::init(deviceName.c_str());
  server = BLEDevice::createServer();
  if (!server) {
    setStatusMessage("BLE server create failed");
    BLEDevice::deinit(true);
    return false;
  }

  serverCallbacks = new (std::nothrow) ServerDisconnectCallbacks();
  if (!serverCallbacks) {
    setStatusMessage("OOM: BLE server callbacks");
    BLEDevice::deinit(true);
    server = nullptr;
    return false;
  }
  server->setCallbacks(serverCallbacks);

  service = server->createService(kServiceUuid);
  if (!service) {
    setStatusMessage("BLE service create failed");
    BLEDevice::deinit(true);
    server = nullptr;
    return false;
  }

  characteristic = service->createCharacteristic(kCharacteristicUuid,
                                                 BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  if (!characteristic) {
    setStatusMessage("BLE characteristic create failed");
    BLEDevice::deinit(true);
    server = nullptr;
    service = nullptr;
    return false;
  }

  callbacks = new (std::nothrow) CredentialCharacteristicCallbacks(this);
  if (!callbacks) {
    setStatusMessage("OOM: BLE characteristic callbacks");
    BLEDevice::deinit(true);
    server = nullptr;
    service = nullptr;
    return false;
  }
  characteristic->setCallbacks(callbacks);
  characteristic->setValue("Send WiFi credentials payload");

  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  running.store(true);
  setStatusMessage("BLE ready: write SSID/password");
  LOG_DBG("BLE", "WiFi provisioning BLE started");
  return true;
}

void BleWifiProvisioner::stop() {
  if (!running.exchange(false)) {
    return;
  }

  BLEDevice::stopAdvertising();
  BLEDevice::deinit(true);

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

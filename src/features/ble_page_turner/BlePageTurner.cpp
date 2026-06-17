#include "BlePageTurner.h"

#if ENABLE_BLE_PAGE_TURNER && !defined(SIMULATOR)

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <HIDKeyboardTypes.h>
#include <HIDTypes.h>
#include <Logging.h>

namespace {
BLEHIDDevice* hidDevice = nullptr;
BLECharacteristic* inputCharacteristic = nullptr;
BLEServer* bleServer = nullptr;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    LOG_INF("BLE Page Turner connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    LOG_INF("BLE Page Turner disconnected");
    // Restart advertising if disconnected
    BLEDevice::startAdvertising();
  }
};

const uint8_t hidReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224)
    0x29, 0xE7,  //   Usage Maximum (231)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute) - Modifier bytes
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant) - Reserved byte
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array) - Key array (6 bytes)
    0xC0         // End Collection
};

void sendKeyPress(uint8_t keycode) {
  if (!deviceConnected || !inputCharacteristic) return;
  uint8_t msg[] = {0x00, 0x00, keycode, 0x00, 0x00, 0x00, 0x00, 0x00};
  inputCharacteristic->setValue(msg, sizeof(msg));
  inputCharacteristic->notify();

  // Send release
  uint8_t release[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  inputCharacteristic->setValue(release, sizeof(release));
  inputCharacteristic->notify();
}
}  // namespace

BlePageTurner::BlePageTurner() {}

BlePageTurner::~BlePageTurner() { stop(); }

void BlePageTurner::start() {
  LOG_INF("Initializing BLE Page Turner (HID)...");
  BLEDevice::init("CrossPoint Reader");
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  hidDevice = new BLEHIDDevice(bleServer);
  inputCharacteristic = hidDevice->inputReport(1);

  hidDevice->manufacturer()->setValue("CrossPoint");
  hidDevice->pnp(0x02, 0xe502, 0xa111, 0x0210);
  hidDevice->hidInfo(0x00, 0x01);

  BLESecurity* pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  hidDevice->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
  hidDevice->startServices();

  BLEAdvertising* advertising = bleServer->getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hidDevice->hidService()->getUUID());
  advertising->start();

  hidDevice->setBatteryLevel(100);
  LOG_INF("BLE Page Turner advertising started.");
}

void BlePageTurner::stop() {
  LOG_INF("Stopping BLE Page Turner...");
  if (bleServer) {
    BLEDevice::stopAdvertising();
    BLEDevice::deinit(true);
    // Cleanup allocated objects
    delete hidDevice;
    hidDevice = nullptr;
    inputCharacteristic = nullptr;
    bleServer = nullptr;
  }
  deviceConnected = false;
}

bool BlePageTurner::isConnected() const { return deviceConnected; }

void BlePageTurner::sendPageNext() {
  LOG_DBG("BLE", "Sending Page Down");
  sendKeyPress(0x4E);  // Page Down keycode
}

void BlePageTurner::sendPagePrev() {
  LOG_DBG("BLE", "Sending Page Up");
  sendKeyPress(0x4B);  // Page Up keycode
}

#endif  // ENABLE_BLE_PAGE_TURNER

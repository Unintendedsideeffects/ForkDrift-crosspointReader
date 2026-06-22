#include "BlePageTurner.h"

#if ENABLE_BLE_PAGE_TURNER && !defined(SIMULATOR)

#include <HIDKeyboardTypes.h>
#include <HIDTypes.h>
#include <Logging.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>

namespace {
NimBLEHIDDevice* hidDevice = nullptr;
NimBLECharacteristic* inputCharacteristic = nullptr;
NimBLEServer* bleServer = nullptr;
bool deviceConnected = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    LOG_INF("BLE", "Page Turner connected");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    LOG_INF("BLE", "Page Turner disconnected");
    // Restart advertising if disconnected
    NimBLEDevice::startAdvertising();
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
  LOG_INF("BLE", "Initializing Page Turner (HID), free heap %u", (unsigned)ESP.getFreeHeap());

  // init() blocks until the controller<->host sync completes and returns false
  // if the BLE controller fails to come up (e.g. heap exhaustion). Proceeding
  // past a failed init would dereference an unsynced stack and panic in the NPL
  // mutex layer, so bail cleanly and leave the feature un-started.
  if (!NimBLEDevice::init("CrossPoint Reader")) {
    LOG_ERR("BLE", "NimBLEDevice::init failed; Page Turner not started");
    return;
  }

  // Security: bonding, no MITM (Just Works), Secure Connections; preserve old intent
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  bleServer = NimBLEDevice::createServer();
  if (!bleServer) {
    LOG_ERR("BLE", "createServer failed; Page Turner not started");
    NimBLEDevice::deinit(true);
    return;
  }
  bleServer->setCallbacks(new (std::nothrow) ServerCallbacks());

  hidDevice = new (std::nothrow) NimBLEHIDDevice(bleServer);
  if (!hidDevice) {
    LOG_ERR("BLE", "OOM: NimBLEHIDDevice; Page Turner not started");
    NimBLEDevice::deinit(true);
    bleServer = nullptr;
    return;
  }
  inputCharacteristic = hidDevice->getInputReport(1);

  hidDevice->setManufacturer("CrossPoint");
  hidDevice->setPnp(0x02, 0xe502, 0xa111, 0x0210);
  hidDevice->setHidInfo(0x00, 0x01);

  hidDevice->setReportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
  hidDevice->startServices();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(HID_KEYBOARD);
  advertising->addServiceUUID(hidDevice->getHidService()->getUUID());
  advertising->start();

  hidDevice->setBatteryLevel(100);
  LOG_INF("BLE", "Page Turner advertising started");
}

void BlePageTurner::stop() {
  LOG_INF("BLE", "Stopping Page Turner");
  if (bleServer) {
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(true);
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

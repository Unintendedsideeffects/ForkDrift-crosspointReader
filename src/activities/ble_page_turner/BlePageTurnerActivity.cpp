#include "activities/ble_page_turner/BlePageTurnerActivity.h"

#include <Logging.h>

#include "fontIds.h"

BlePageTurnerActivity::BlePageTurnerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("BlePageTurner", renderer, mappedInput) {}

BlePageTurnerActivity::~BlePageTurnerActivity() = default;

void BlePageTurnerActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("BLE", "BlePageTurnerActivity: Entering, initializing BLE HID stack");

  // Start the BLE HID server
  blePageTurner.start();
  requestUpdate();
}

void BlePageTurnerActivity::onExit() {
  LOG_INF("BLE", "BlePageTurnerActivity: Exiting, tearing down BLE HID stack");

  // Stop the BLE HID server to free RAM
  blePageTurner.stop();
  Activity::onExit();
}

void BlePageTurnerActivity::loop() {
  // Check if connection status changed to update UI
  bool newConnectedState = blePageTurner.isConnected();
  if (newConnectedState != isConnected) {
    isConnected = newConnectedState;
    requestUpdate();
  }

  // Handle inputs
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (isConnected) {
    if (mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
        mappedInput.wasPressed(MappedInputManager::Button::Right) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      blePageTurner.sendPageNext();
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
               mappedInput.wasPressed(MappedInputManager::Button::Left) ||
               mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      blePageTurner.sendPagePrev();
      requestUpdate();
    }
  }
}

void BlePageTurnerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  int y = 50;
  renderer.drawText(UI_12_FONT_ID, 10, y, "BLE Page Turner");
  y += 40;

  if (isConnected) {
    renderer.drawText(UI_12_FONT_ID, 10, y, "Status: CONNECTED");
    y += 30;
    renderer.drawText(UI_12_FONT_ID, 10, y, "Use buttons to turn pages.");
  } else {
    renderer.drawText(UI_12_FONT_ID, 10, y, "Status: ADVERTISING...");
    y += 30;
    renderer.drawText(UI_12_FONT_ID, 10, y, "Pair with 'CrossPoint Reader' on your phone.");
  }

  y += 50;
  renderer.drawText(SMALL_FONT_ID, 10, y, "Press BACK to exit and free BLE memory.");
}

#include "features/trmnl_switch/Registration.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>

#if ENABLE_TRMNL_SWITCH

#include <Arduino.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "images/Logo120.h"

#endif  // ENABLE_TRMNL_SWITCH

namespace features::trmnl_switch {

#if ENABLE_TRMNL_SWITCH

namespace {

void logIfRunningOnNonDefaultOtaSlot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running != nullptr && running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
    LOG_WRN("TRMNL", "running on unexpected partition; auto-switch may bounce");
  }
}

void showTrmnlSwitchSplash(GfxRenderer& renderer) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 + 70, tr(STR_SWITCHING_TO_TRMNL), true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  delay(500);
}

}  // namespace

#endif  // ENABLE_TRMNL_SWITCH

void registerFeature() {}

void maybeBootToTrmnl(bool usbConnectedAtBoot, GfxRenderer& renderer) {
#if ENABLE_TRMNL_SWITCH
  logIfRunningOnNonDefaultOtaSlot();
  if (!usbConnectedAtBoot) {
    return;
  }
  if (SETTINGS.bootToTrmnlOnCharge == 0) {
    return;
  }
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (next == nullptr) {
    return;
  }
  showTrmnlSwitchSplash(renderer);
  LOG_INF("TRMNL", "Switching to next partition: %s", next->label);
  esp_ota_set_boot_partition(next);
  esp_restart();
#else
  (void)usbConnectedAtBoot;
  (void)renderer;
#endif
}

}  // namespace features::trmnl_switch

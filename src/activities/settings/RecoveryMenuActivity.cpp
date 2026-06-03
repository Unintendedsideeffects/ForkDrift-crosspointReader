#include "RecoveryMenuActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "ClearCacheActivity.h"
#include "FactoryResetActivity.h"
#include "MappedInputManager.h"
#include "ResetSettingsActivity.h"
#include "SdFirmwareUpdateActivity.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

std::string RecoveryMenuActivity::itemLabel(const int index) {
  switch (index) {
    case RecoveryMenuActivity::FirmwareUpdate:
      return std::string(tr(STR_SD_FIRMWARE_UPDATE));
    case RecoveryMenuActivity::ClearCache:
      return std::string(tr(STR_CLEAR_READING_CACHE));
    case RecoveryMenuActivity::ResetSettings:
      return std::string(tr(STR_RESET_SETTINGS));
    case RecoveryMenuActivity::FactoryReset:
      return std::string(tr(STR_FACTORY_RESET));
    case RecoveryMenuActivity::Restart:
      return std::string(tr(STR_RECOVERY_RESTART));
    default:
      return "";
  }
}

void RecoveryMenuActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void RecoveryMenuActivity::activateSelected() {
  const auto onReturn = [this](const ActivityResult&) { requestUpdate(); };

  switch (selectedIndex) {
    case FirmwareUpdate:
      // Non-recovery mode: Back returns to this menu instead of re-trapping.
      startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), onReturn);
      break;
    case ClearCache:
      startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), onReturn);
      break;
    case ResetSettings:
      startActivityForResult(std::make_unique<ResetSettingsActivity>(renderer, mappedInput), onReturn);
      break;
    case FactoryReset:
      startActivityForResult(
          std::make_unique<FactoryResetActivity>(renderer, mappedInput, [] { activityManager.popActivity(); }),
          onReturn);
      break;
    case Restart:
      LOG_INF("RECOVERY", "User requested restart from recovery menu");
      ESP.restart();
      break;
    default:
      break;
  }
}

void RecoveryMenuActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return;
  }

  // Exit recovery via a clean reboot back into the normal boot path.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    LOG_INF("RECOVERY", "Exiting recovery menu (restart)");
    ESP.restart();
    return;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ItemCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ItemCount);
    requestUpdate();
  });
}

void RecoveryMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_RECOVERY_MODE),
                 nullptr);

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ItemCount, selectedIndex,
      [](int index) { return RecoveryMenuActivity::itemLabel(index); }, nullptr, nullptr, nullptr);

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

#include "TerminusSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "SpiBusMutex.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "util/TerminusApi.h"
#include "util/TerminusCredentialStore.h"

void TerminusSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  baseUrl = TERMINUS_STORE.baseUrl();
  apiToken = TERMINUS_STORE.apiKey();
  errorMessage.clear();
  requestUpdate();
}

void TerminusSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedIndex = (selectedIndex + 1) % kMenuItems;
    errorMessage.clear();
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = (selectedIndex + kMenuItems - 1) % kMenuItems;
    errorMessage.clear();
    requestUpdate();
  });
}

void TerminusSettingsActivity::handleSelection() {
  errorMessage.clear();
  if (selectedIndex == 0) {
    const std::string initialUrl = baseUrl.empty() ? "https://" : baseUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TERMINUS_SERVER_URL),
                                                                   initialUrl, 160, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               baseUrl = std::get<KeyboardResult>(result.data).text;
                             }
                           });
    return;
  }

  if (selectedIndex == 1) {
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TERMINUS_API_TOKEN),
                                                                   apiToken, 128, InputType::Password),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               apiToken = std::get<KeyboardResult>(result.data).text;
                             }
                           });
    return;
  }

  saveSetup();
}

void TerminusSettingsActivity::saveSetup() {
  const std::string normalizedUrl = terminus_api::normalizeBaseUrl(baseUrl);
  if (apiToken.empty() || !terminus_api::isAllowedRemoteUrl(normalizedUrl)) {
    errorMessage = tr(STR_TERMINUS_SETUP_INVALID);
    requestUpdate();
    return;
  }

  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char deviceId[18] = {};
  snprintf(deviceId, sizeof(deviceId), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  TERMINUS_STORE.setBaseUrl(normalizedUrl);
  TERMINUS_STORE.setApiKey(apiToken);
  TERMINUS_STORE.setDeviceId(deviceId);
  TERMINUS_STORE.setDeviceModel("xteink_x4");

  bool saved = false;
  {
    SpiBusMutex::Guard guard;
    saved = TERMINUS_STORE.save();
  }
  if (!saved) {
    LOG_ERR("TERMINUS", "Failed to save on-device Terminus setup");
    errorMessage = tr(STR_ERROR_GENERAL_FAILURE);
    requestUpdate();
    return;
  }

  LOG_INF("TERMINUS", "On-device setup saved for model=xteink_x4");
  finish();
}

void TerminusSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TERMINUS_SETUP));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, kMenuItems, selectedIndex,
      [](int index) {
        if (index == 0) return std::string(tr(STR_TERMINUS_SERVER_URL));
        if (index == 1) return std::string(tr(STR_TERMINUS_API_TOKEN));
        return std::string(tr(STR_SAVE_TERMINUS_SETUP));
      },
      nullptr, nullptr,
      [this](int index) {
        if (index == 0) return baseUrl;
        if (index == 1) return apiToken.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        return std::string();
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (!errorMessage.empty()) {
    GUI.drawPopup(renderer, errorMessage.c_str());
  }
  renderer.displayBuffer();
}

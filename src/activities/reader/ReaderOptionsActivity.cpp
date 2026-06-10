#include "ReaderOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/ActivityResult.h"
#include "activities/settings/SettingsTopics.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"

namespace {

uint8_t readEnumValue(const SettingInfo& setting) {
  if (setting.valueGetter) {
    return setting.valueGetter();
  }
  if (setting.valuePtr != nullptr) {
    return SETTINGS.*(setting.valuePtr);
  }
  return 0;
}

void writeEnumValue(const SettingInfo& setting, const uint8_t value) {
  if (setting.valueSetter) {
    setting.valueSetter(value);
    return;
  }
  if (setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = value;
  }
}

size_t enumOptionCount(const SettingInfo& setting) {
  if (!setting.enumStringValues.empty()) {
    return setting.enumStringValues.size();
  }
  if (setting.dynamicValuesGetter) {
    return setting.dynamicValuesGetter().size();
  }
  return setting.enumValues.size();
}

std::string enumOptionLabel(const SettingInfo& setting, const uint8_t index) {
  if (!setting.enumStringValues.empty()) {
    return index < setting.enumStringValues.size() ? setting.enumStringValues[index] : std::string();
  }
  if (setting.dynamicValuesGetter) {
    const auto values = setting.dynamicValuesGetter();
    return index < values.size() ? values[index] : std::string();
  }
  if (index < setting.enumValues.size()) {
    return I18N.get(setting.enumValues[index]);
  }
  return std::string();
}

}  // namespace

void ReaderOptionsActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.refreshIfDirty();
  rebuildSettingsList();
  requestUpdate();
}

void ReaderOptionsActivity::onExit() { Activity::onExit(); }

void ReaderOptionsActivity::rebuildSettingsList() {
  settings.clear();

  const auto allSettings = getSettingsList(&sdFontSystem.registry());
  settings.reserve(allSettings.size());
  std::copy_if(allSettings.begin(), allSettings.end(), std::back_inserter(settings),
               [](const SettingInfo& setting) { return setting.category == StrId::STR_CAT_READER; });

  groupSettingsByTopic(settings, settings_topics::kReader);

  settingsCount = static_cast<int>(settings.size());
  selectedIndex = 0;
  while (selectedIndex < settingsCount && settings[selectedIndex].type == SettingType::SECTION_HEADER) {
    selectedIndex++;
  }
  if (selectedIndex >= settingsCount) {
    selectedIndex = 0;
  }
}

void ReaderOptionsActivity::moveSelection(bool forward) {
  if (settingsCount <= 0) return;

  for (int i = 0; i < settingsCount; i++) {
    selectedIndex = forward ? ButtonNavigator::nextIndex(selectedIndex, settingsCount)
                            : ButtonNavigator::previousIndex(selectedIndex, settingsCount);
    if (settings[selectedIndex].type != SettingType::SECTION_HEADER) {
      break;
    }
  }
}

bool ReaderOptionsActivity::settingAffectsPreview(const SettingInfo& setting) {
  if (setting.type == SettingType::SECTION_HEADER || setting.type == SettingType::ACTION) {
    return false;
  }
  if (setting.key == nullptr) {
    return true;
  }
  return std::strcmp(setting.key, "hideBatteryPercentage") != 0 &&
         std::strcmp(setting.key, "globalStatusBarPosition") != 0;
}

void ReaderOptionsActivity::refreshPreviewIfNeeded(const SettingInfo& setting) {
  if (!settingAffectsPreview(setting) || !previewRefresh_ || pageBuffer_ == nullptr) {
    return;
  }
  previewRefresh_(pageBuffer_, renderer.getBufferSize());
}

void ReaderOptionsActivity::toggleCurrentSetting() {
  if (selectedIndex < 0 || selectedIndex >= settingsCount) return;
  const auto& setting = settings[selectedIndex];

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    const bool cur = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !cur;
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("RDR", "Failed to save settings");
    }
    readerSettingsChanged_ = true;
    refreshPreviewIfNeeded(setting);
  } else if (setting.type == SettingType::ENUM) {
    const size_t optionCount = enumOptionCount(setting);
    if (optionCount == 0) {
      return;
    }
    const uint8_t cur = readEnumValue(setting);
    const uint8_t maxIndex = static_cast<uint8_t>(optionCount - 1);
    uint8_t newValue;
    if (cur > maxIndex) {
      newValue = 0;
    } else {
      newValue = (cur + 1) % static_cast<uint8_t>(optionCount);
    }
    writeEnumValue(setting, newValue);
    if (setting.key != nullptr && std::strcmp(setting.key, "fontFamily") == 0) {
      core::FeatureModules::onFontFamilySettingChanged(SETTINGS.fontFamily);
    }
    if (setting.key != nullptr && std::strcmp(setting.key, "orientation") == 0) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("RDR", "Failed to save settings");
    }
    readerSettingsChanged_ = true;
    refreshPreviewIfNeeded(setting);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t cur = SETTINGS.*(setting.valuePtr);
    if (cur + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = cur + setting.valueRange.step;
    }
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("RDR", "Failed to save settings");
    }
    readerSettingsChanged_ = true;
    refreshPreviewIfNeeded(setting);
  }
}

void ReaderOptionsActivity::loop() {
  buttonNavigator.onNextRelease([this] {
    moveSelection(true);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    moveSelection(false);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("RDR", "Failed to save settings");
    }
    setResult(ControlsOptionsResult{readerSettingsChanged_});
    finish();
    return;
  }
}

void ReaderOptionsActivity::render(RenderLock&&) {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.buttonHintsHeight : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;

  auto rowTitle = [this](int i) { return std::string(I18N.get(settings[i].nameId)); };
  auto isHeader = [this](int i) { return settings[i].type == SettingType::SECTION_HEADER; };
  auto rowValue = [this](int i) {
    const auto& setting = settings[i];
    std::string valueText;
    if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
      valueText = SETTINGS.*(setting.valuePtr) ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    } else if (setting.type == SettingType::ENUM) {
      const uint8_t value = readEnumValue(setting);
      const uint8_t maxIndex = static_cast<uint8_t>(enumOptionCount(setting));
      const uint8_t safeValue = maxIndex > 0 && value < maxIndex ? value : 0;
      valueText = enumOptionLabel(setting, safeValue);
    } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
      valueText = std::to_string(SETTINGS.*(setting.valuePtr));
    }
    return valueText;
  };

  if (pageBuffer_ || previewRefresh_) {
    if (!pageBuffer_) {
      LOG_INF("RDR", "ReaderOptions: half-screen preview mode (rebuilt)");
      previewRefresh_(renderer.getFrameBuffer(), renderer.getBufferSize());
    } else {
      LOG_INF("RDR", "ReaderOptions: half-screen preview mode (cached)");
      memcpy(renderer.getFrameBuffer(), pageBuffer_, renderer.getBufferSize());
    }
    const int panelY = pageHeight / 2;
    renderer.fillRect(0, panelY, pageWidth, pageHeight - panelY, false);
    renderer.drawLine(0, panelY, pageWidth - 1, panelY, true);

    const int listTop = panelY + metrics.verticalSpacing;
    const int listHeight = pageHeight - panelY - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    GUI.drawList(renderer, Rect{contentX, listTop, contentWidth, listHeight}, settingsCount, selectedIndex, rowTitle,
                 nullptr, nullptr, rowValue, true, nullptr, isHeader);
  } else {
    LOG_INF("RDR", "ReaderOptions: full-screen fallback mode");
    renderer.clearScreen();
    GUI.drawHeader(renderer, Rect{contentX, metrics.topPadding, contentWidth, metrics.headerHeight}, tr(STR_CAT_READER),
                   nullptr);
    const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int listHeight = pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight +
                                         metrics.verticalSpacing * 2);
    GUI.drawList(renderer, Rect{contentX, listTop, contentWidth, listHeight}, settingsCount, selectedIndex, rowTitle,
                 nullptr, nullptr, rowValue, true, nullptr, isHeader);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);

  renderer.displayBuffer();
}

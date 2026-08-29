#include "ReaderOptionsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#if ENABLE_PER_BOOK_SETTINGS
#include "util/BookSettingsOverride.h"
#endif
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "activities/ActivityResult.h"
#include "activities/settings/SettingsTopics.h"
#include "activities/util/ListPickerActivity.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiService.h"

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

namespace {
// Persist a setting change made in this overlay: while per-book mode is on,
// per-book-capable keys record into the book override instead of the globals.
void persistOverlayChange(const char* key) {
#if ENABLE_PER_BOOK_SETTINGS
  if (BookSettingsScope::isEnabled() && BookSettingsScope::recordChange(key)) {
    return;  // recorded into the book's override file
  }
#endif
  // saveToFile is override-aware (snapshot-restoring) when per-book mode is on.
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("RDR", "Failed to save settings");
  }
}
}  // namespace

void ReaderOptionsActivity::onEnter() {
  Activity::onEnter();
  if (BG_WIFI.isPendingOrRunning()) {
    BG_WIFI.stop(true);
  }
  BackgroundWebServer::getInstance().stop(true);
  rebuildSettingsList();
  requestUpdate();
}

void ReaderOptionsActivity::onExit() { Activity::onExit(); }

bool ReaderOptionsActivity::rebuildSettingsList() {
  settings.clear();
  sdFontSystem.refreshIfDirty();

  const bool hasSleepImages = dirHasAnyImage("/sleep");
  const bool hasPokedexImages = dirHasAnyImage("/sleep/pokedex");
  const StrId readerCategory = StrId::STR_CAT_READER;
  settings.reserve(24);
  forEachSetting(
      [](void* ctx, SettingInfo&& info) { static_cast<std::vector<SettingInfo>*>(ctx)->push_back(std::move(info)); },
      &settings, hasSleepImages, hasPokedexImages, buildFontFamilySetting(&sdFontSystem.registry()), &readerCategory);

  groupSettingsByTopic(settings, settings_topics::kReader);

#if ENABLE_PER_BOOK_SETTINGS
  if (BookSettingsScope::isActive()) {
    // "For this book only": while ON, changes made in this overlay record into
    // the book's override file; globals on disk stay untouched.
    settings.insert(settings.begin(), SettingInfo::DynamicEnum(
                                          StrId::STR_PER_BOOK_SETTINGS, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
                                          [] { return BookSettingsScope::isEnabled() ? 1 : 0; },
                                          [this](const uint8_t v) {
                                            BookSettingsScope::setEnabled(v != 0);
                                            readerSettingsChanged_ = true;  // relayout with restored globals
                                          },
                                          "perBookToggle"));
  }
#endif

  settingsCount = static_cast<int>(settings.size());
  selectedIndex = 0;
  while (selectedIndex < settingsCount && settings[selectedIndex].type == SettingType::SECTION_HEADER) {
    selectedIndex++;
  }
  if (selectedIndex >= settingsCount) {
    selectedIndex = 0;
  }
  return true;
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
    persistOverlayChange(setting.key);
    readerSettingsChanged_ = true;
    refreshPreviewIfNeeded(setting);
  } else if (setting.type == SettingType::ENUM) {
    const size_t optionCount = enumOptionCount(setting);
    if (optionCount == 0) {
      return;
    }

    if (optionCount > 4) {
      std::vector<std::string> items;
      items.reserve(optionCount);
      for (size_t i = 0; i < optionCount; ++i) {
        items.push_back(enumOptionLabel(setting, i));
      }
      const uint8_t cur = readEnumValue(setting);
      const uint8_t maxIndex = static_cast<uint8_t>(optionCount - 1);
      const uint8_t safeIndex = cur <= maxIndex ? cur : 0;

      startActivityForResult(
          std::make_unique<ListPickerActivity>(renderer, mappedInput, setting.nameId, std::move(items), safeIndex),
          [this, setting, optionCount](const ActivityResult& result) {
            if (!result.isCancelled && std::holds_alternative<ListPickerResult>(result.data)) {
              const int idx = std::get<ListPickerResult>(result.data).selectedIndex;
              if (idx >= 0 && idx < static_cast<int>(optionCount)) {
                const uint8_t newValue = static_cast<uint8_t>(idx);
                writeEnumValue(setting, newValue);
                if (setting.key != nullptr && std::strcmp(setting.key, "fontFamily") == 0) {
                  core::FeatureModules::onFontFamilySettingChanged(SETTINGS.fontFamily);
                }
                if (setting.key != nullptr && std::strcmp(setting.key, "orientation") == 0) {
                  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
                }
                persistOverlayChange(setting.key);
                readerSettingsChanged_ = true;
                refreshPreviewIfNeeded(setting);
              }
            }
            requestUpdate();
          });
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
    persistOverlayChange(setting.key);
    readerSettingsChanged_ = true;
    refreshPreviewIfNeeded(setting);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t cur = SETTINGS.*(setting.valuePtr);
    if (cur + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = cur + setting.valueRange.step;
    }
    persistOverlayChange(setting.key);
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
    persistOverlayChange(nullptr);
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
#ifdef SIMULATOR
    extern bool g_sim_reader_options_full_screen;
    g_sim_reader_options_full_screen = false;
#endif
    renderer.fillRect(0, panelY, pageWidth, pageHeight - panelY, false);
    renderer.drawLine(0, panelY, pageWidth - 1, panelY, true);

    const int listTop = panelY + metrics.verticalSpacing;
    const int listHeight = pageHeight - panelY - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    GUI.drawList(renderer, Rect{contentX, listTop, contentWidth, listHeight}, settingsCount, selectedIndex, rowTitle,
                 nullptr, nullptr, rowValue, true, nullptr, isHeader);
  } else {
#ifdef SIMULATOR
    extern bool g_sim_reader_options_full_screen;
    g_sim_reader_options_full_screen = true;
#endif
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

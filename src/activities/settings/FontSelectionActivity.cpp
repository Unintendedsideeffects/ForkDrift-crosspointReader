#include "FontSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "FeatureFlags.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  // Build combined font list: built-in + SD card fonts
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0) +
                 (registry_ ? registry_->getFamilyCount() : 0));

  fonts_.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, 0});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, 1});
  fonts_.push_back({I18N.get(StrId::STR_OPEN_DYSLEXIC), true, 2});
#if ENABLE_LEXENDDECA_FONTS
  fonts_.push_back({I18N.get(StrId::STR_LEXEND_DECA), true, CrossPointSettings::LEXENDDECA});
#endif
#if ENABLE_BITTER_FONTS
  fonts_.push_back({I18N.get(StrId::STR_BITTER), true, CrossPointSettings::BITTER});
#endif
#if ENABLE_CHAREINK_FONTS
  fonts_.push_back({I18N.get(StrId::STR_CHARE_INK), true, CrossPointSettings::CHAREINK});
#endif
  if (hasUserFonts) {
    fonts_.push_back({I18N.get(StrId::STR_EXTERNAL_FONT), false, CrossPointSettings::USER_SD});
  }

  if (registry_) {
    const auto& families = registry_->getFamilies();
    const uint8_t sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(sdBase + i)});
    }
  }

  // Find current selection
  selectedIndex_ = 0;
  if (hasUserFonts && SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
    selectedIndex_ = CrossPointSettings::BUILTIN_FONT_COUNT;
  } else if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    const int sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        selectedIndex_ = sdBase + i;
        break;
      }
    }
  } else {
    selectedIndex_ = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
}

void FontSelectionActivity::handleSelection() {
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
  const auto& font = fonts_[selectedIndex_];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (hasUserFonts && font.settingIndex == CrossPointSettings::USER_SD) {
    SETTINGS.fontFamily = CrossPointSettings::USER_SD;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    const int sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
    int sdIdx = font.settingIndex - sdBase;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      if (SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
        SETTINGS.fontFamily = CrossPointSettings::BOOKERLY;
      }
    }
  }
  core::FeatureModules::onFontFamilySettingChanged(SETTINGS.fontFamily);
  finish();
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_FAMILY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Determine which font index is currently active (to mark as "Selected")
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
  int currentFontIndex = 0;
  if (hasUserFonts && SETTINGS.fontFamily == CrossPointSettings::USER_SD) {
    currentFontIndex = CrossPointSettings::BUILTIN_FONT_COUNT;
  } else if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    const int sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        currentFontIndex = sdBase + i;
        break;
      }
    }
  } else {
    currentFontIndex = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
      [this, currentFontIndex](int index) -> std::string { return index == currentFontIndex ? tr(STR_SELECTED) : ""; },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

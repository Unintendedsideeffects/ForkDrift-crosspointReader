#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "FeatureFlags.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "core/features/FeatureModules.h"
#include "fontIds.h"

namespace {
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";
}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  originalFontFamily_ = SETTINGS.fontFamily;
  strncpy(originalSdFontFamilyName_, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName_) - 1);
  originalSdFontFamilyName_[sizeof(originalSdFontFamilyName_) - 1] = '\0';

  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0) +
                 (registry_ ? registry_->getFamilyCount() : 0));

#if ENABLE_BOOKERLY_FONTS
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, CrossPointSettings::NOTOSERIF});
#endif
#if ENABLE_NOTOSANS_FONTS
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, CrossPointSettings::NOTOSANS});
#endif
#if ENABLE_OPENDYSLEXIC_FONTS
  fonts_.push_back({I18N.get(StrId::STR_OPEN_DYSLEXIC), true, CrossPointSettings::OPENDYSLEXIC});
#endif
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

  selectedIndex_ = findSelectionIndexFor(SETTINGS.sdFontFamilyName, SETTINGS.fontFamily, hasUserFonts);
  previewFontIndex_ = selectedIndex_;

  requestUpdate();
}

int FontSelectionActivity::findSelectionIndexFor(const char* sdFontFamilyName, uint8_t fontFamily,
                                                 bool hasUserFonts) const {
  if (hasUserFonts && fontFamily == CrossPointSettings::USER_SD) {
    for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
      if (fonts_[i].settingIndex == CrossPointSettings::USER_SD) {
        return i;
      }
    }
    return 0;
  }

  if (sdFontFamilyName[0] != '\0') {
    for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
      if (!fonts_[i].isBuiltin && fonts_[i].settingIndex != CrossPointSettings::USER_SD &&
          fonts_[i].name == sdFontFamilyName) {
        return i;
      }
    }
    return 0;
  }

  for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
    if (fonts_[i].settingIndex == fontFamily) {
      return i;
    }
  }
  return 0;
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.fontFamily = originalFontFamily_;
    strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName_, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex_ == previewFontIndex_) {
      handleSelection();
    } else {
      previewFontIndex_ = selectedIndex_;
      const auto& font = fonts_[selectedIndex_];
      if (font.isBuiltin) {
        SETTINGS.fontFamily = font.settingIndex;
        SETTINGS.sdFontFamilyName[0] = '\0';
      } else if (font.settingIndex == CrossPointSettings::USER_SD) {
        SETTINGS.fontFamily = CrossPointSettings::USER_SD;
        SETTINGS.sdFontFamilyName[0] = '\0';
      } else if (registry_) {
        const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);
        const int sdBase = CrossPointSettings::BUILTIN_FONT_COUNT + (hasUserFonts ? 1 : 0);
        const int sdIdx = font.settingIndex - sdBase;
        const auto& families = registry_->getFamilies();
        if (sdIdx >= 0 && sdIdx < static_cast<int>(families.size())) {
          strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
          SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
          sdFontSystem.ensureLoaded(renderer);
        }
      }
      requestUpdate();
    }
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing);

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
    const int sdIdx = font.settingIndex - sdBase;
    const auto& families = registry_->getFamilies();
    if (sdIdx >= 0 && sdIdx < static_cast<int>(families.size())) {
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

void FontSelectionActivity::renderPreviewPane(int top, int height, int fontId, const char* fontName) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelFontId = UI_10_FONT_ID;
  const int labelH = renderer.getTextHeight(labelFontId);
  const int labelGap = 4;
  const int labelReserved = labelH + labelGap + metrics_.previewPadding;

  char labelBuf[128];
  snprintf(labelBuf, sizeof(labelBuf), "%s \"%s\"", tr(STR_PREVIEW), fontName ? fontName : "");
  const int labelY = top + height - metrics_.previewPadding - labelH;
  renderer.drawText(labelFontId, left, labelY, labelBuf);

  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int innerHeight = height - metrics_.previewPadding - labelReserved;
  const int maxLines = std::max(1, innerHeight / (lineH + 2));

  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* fcm = renderer.getFontCacheManager()) {
    char prewarmBuf[256];
    snprintf(prewarmBuf, sizeof(prewarmBuf), "%s %s", previewText, ELLIPSIS_UTF8);
    fcm->prewarmCache(fontId, prewarmBuf, 0x01);
  }

  const auto lines = renderer.wrappedText(fontId, previewText, width, maxLines);

  int y = top + metrics_.previewPadding;
  const int textBottomLimit = top + height - labelReserved;
  for (const auto& line : lines) {
    if (y + lineH > textBottomLimit) break;
    renderer.drawText(fontId, left, y, line.c_str());
    y += lineH + 2;
  }
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const bool hasUserFonts = core::FeatureModules::hasCapability(core::Capability::UserFonts);

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_FONT_FAMILY));

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;
  const int listHeight = usableHeight - previewHeight - metrics_.verticalSpacing;

  const int previewFontId = SETTINGS.getReaderFontId();
  const char* previewFontName = (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size()))
                                    ? fonts_[previewFontIndex_].name.c_str()
                                    : nullptr;
  renderPreviewPane(previewTop, previewHeight, previewFontId, previewFontName);

  renderer.drawLine(0, listTop - metrics_.verticalSpacing / 2, pageWidth, listTop - metrics_.verticalSpacing / 2);

  const int savedFontIndex = findSelectionIndexFor(originalSdFontFamilyName_, originalFontFamily_, hasUserFonts);
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
      [this, savedFontIndex](int index) -> std::string {
        if (index == previewFontIndex_ && index != savedFontIndex) return tr(STR_PREVIEW);
        if (index == savedFontIndex) return tr(STR_SELECTED);
        return "";
      },
      true);

  const bool onPreviewed = selectedIndex_ == previewFontIndex_;
  const char* confirmLabel = onPreviewed ? tr(STR_SELECT) : tr(STR_PREVIEW);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

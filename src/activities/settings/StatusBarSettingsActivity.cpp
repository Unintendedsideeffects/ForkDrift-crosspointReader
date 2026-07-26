#include "StatusBarSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <cstring>
#include <memory>

#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "features/status_overlay/Layout.h"
#include "features/status_overlay/ReaderContext.h"
#include "fontIds.h"

namespace {
// Base items shown on every device. The two clock items are appended only when the
// DS3231 RTC is present (X3), so X4 never sees them (visibleItemCount in onEnter()).
constexpr int BASE_MENU_ITEMS = 6;
enum MenuItem {
  ITEM_CHAPTER_PAGE_COUNT = 0,
  ITEM_BOOK_PROGRESS_PERCENTAGE,
  ITEM_PROGRESS_BAR,
  ITEM_PROGRESS_BAR_THICKNESS,
  ITEM_TITLE,
  ITEM_BATTERY,
  ITEM_CLOCK_FORMAT,  // X3 only
  ITEM_CLOCK_SYNC,    // X3 only, launches ClockSyncActivity
  ITEM_COUNT
};
const StrId menuNames[ITEM_COUNT] = {StrId::STR_CHAPTER_PAGE_COUNT,
                                     StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                     StrId::STR_PROGRESS_BAR,
                                     StrId::STR_PROGRESS_BAR_THICKNESS,
                                     StrId::STR_TITLE,
                                     StrId::STR_BATTERY,
                                     StrId::STR_CLOCK_FORMAT,
                                     StrId::STR_CLOCK_SYNC};
constexpr int CLOCK_FORMAT_ITEMS = 2;
const StrId clockFormatNames[CLOCK_FORMAT_ITEMS] = {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H};
constexpr int PROGRESS_BAR_ITEMS = 3;
const StrId progressBarNames[PROGRESS_BAR_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

constexpr int PROGRESS_BAR_THICKNESS_ITEMS = 3;
const StrId progressBarThicknessNames[PROGRESS_BAR_THICKNESS_ITEMS] = {
    StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK};

constexpr int TITLE_ITEMS = 3;
const StrId titleNames[TITLE_ITEMS] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};

const int widthMargin = 10;
const int verticalPreviewPadding = 50;
const int verticalPreviewTextPadding = 40;
}  // namespace

void StatusBarSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  // Clock entries are X3-only (DS3231 RTC); hide them entirely on X4.
  visibleItemCount = halClock.isAvailable() ? ITEM_COUNT : BASE_MENU_ITEMS;

  if (SETTINGS.clockFormat >= CLOCK_FORMAT_ITEMS) {
    SETTINGS.clockFormat = 0;
  }

  // Clamp statusBarProgressBar and statusBarTitle in case of corrupt/migrated data
  if (SETTINGS.statusBarProgressBar >= PROGRESS_BAR_ITEMS) {
    SETTINGS.statusBarProgressBar = CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  }

  if (SETTINGS.statusBarProgressBarThickness >= PROGRESS_BAR_THICKNESS_ITEMS) {
    SETTINGS.statusBarProgressBarThickness = CrossPointSettings::STATUS_BAR_PROGRESS_BAR_THICKNESS::PROGRESS_BAR_NORMAL;
  }

  if (SETTINGS.statusBarTitle >= TITLE_ITEMS) {
    SETTINGS.statusBarTitle = CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE;
  }

  requestUpdate();
}

void StatusBarSettingsActivity::onExit() {
  // The live preview calls GUI.drawStatusBar(), which (with the global bar on)
  // publishes preview values into the shared ReaderContext. Clear it so the
  // overlay never renders stale preview data on non-reader screens.
  features::status_overlay::clearReaderContext();
  Activity::onExit();
}

void StatusBarSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }

  // Handle navigation
  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, visibleItemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, visibleItemCount);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, visibleItemCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, visibleItemCount);
    requestUpdate();
  });
}

void StatusBarSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    // Chapter Page Count
    SETTINGS.statusBarChapterPageCount = (SETTINGS.statusBarChapterPageCount + 1) % 2;
  } else if (selectedIndex == 1) {
    // Book Progress %
    SETTINGS.statusBarBookProgressPercentage = (SETTINGS.statusBarBookProgressPercentage + 1) % 2;
  } else if (selectedIndex == 2) {
    // Progress Bar
    SETTINGS.statusBarProgressBar = (SETTINGS.statusBarProgressBar + 1) % PROGRESS_BAR_ITEMS;
  } else if (selectedIndex == 3) {
    // Progress Bar Thickness
    SETTINGS.statusBarProgressBarThickness =
        (SETTINGS.statusBarProgressBarThickness + 1) % PROGRESS_BAR_THICKNESS_ITEMS;
  } else if (selectedIndex == 4) {
    // Chapter Title
    SETTINGS.statusBarTitle = (SETTINGS.statusBarTitle + 1) % TITLE_ITEMS;
  } else if (selectedIndex == 5) {
    // Show Battery
    SETTINGS.statusBarBattery = (SETTINGS.statusBarBattery + 1) % 2;
  } else if (selectedIndex == ITEM_CLOCK_FORMAT) {
    // Clock display format (X3): 24-hour / 12-hour
    SETTINGS.clockFormat = (SETTINGS.clockFormat + 1) % CLOCK_FORMAT_ITEMS;
  } else if (selectedIndex == ITEM_CLOCK_SYNC) {
    // Force a manual NTP re-sync of the RTC. ClockSyncActivity persists its own
    // state, so there is nothing to save here.
    startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput), nullptr);
    return;
  }
  const bool saved = SETTINGS.saveToFile();
  if (!saved) {
    LOG_ERR("SET", "Failed to save status bar settings");
  }
}

void StatusBarSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CUSTOMISE_STATUS_BAR));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(visibleItemCount),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
      nullptr,
      [this](int index) {
        // Draw status for each setting
        if (index == 0) {
          return SETTINGS.statusBarChapterPageCount ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == 1) {
          return SETTINGS.statusBarBookProgressPercentage ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == 2) {
          return I18N.get(progressBarNames[SETTINGS.statusBarProgressBar]);
        } else if (index == 3) {
          return I18N.get(progressBarThicknessNames[SETTINGS.statusBarProgressBarThickness]);
        } else if (index == 4) {
          return I18N.get(titleNames[SETTINGS.statusBarTitle]);
        } else if (index == 5) {
          return SETTINGS.statusBarBattery ? tr(STR_SHOW) : tr(STR_HIDE);
        } else if (index == ITEM_CLOCK_FORMAT) {
          return I18N.get(clockFormatNames[SETTINGS.clockFormat % CLOCK_FORMAT_ITEMS]);
        } else if (index == ITEM_CLOCK_SYNC) {
          return "";  // action item — launches the sync screen
        } else {
          return tr(STR_HIDE);
        }
      },
      true);

  // Draw button hints
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  std::string title;
  if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = tr(STR_EXAMPLE_BOOK);
  } else if (SETTINGS.statusBarTitle == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_EXAMPLE_CHAPTER);
  }

  GUI.drawStatusBar(renderer, 75, 8, 32, title, false);

  const int previewBand = features::status_overlay::topInset();
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding,
                    renderer.getScreenHeight() - previewBand - verticalPreviewTextPadding, tr(STR_PREVIEW));

  renderer.displayBuffer();
}

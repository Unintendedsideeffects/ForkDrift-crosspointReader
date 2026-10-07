#ifdef SIMULATOR

#include "SimulatorSmokeTest.h"

#include <Epub.h>
#include <HalScreenCalibration.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>

#include "OpdsCatalogSmokeTest.h"
#include "ReadingUploadSmokeTest.h"
#include "StatsUploadSmokeTest.h"
#if CROSSINK_SCALABLE_FONTS
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <HalScalableFont.h>

#include <fstream>

#include "FontInstaller.h"
#include "TtfRenderProfileStore.h"
#include "util/WordSelectNavigator.h"
#endif
#include <AppVersion.h>
#include <ArduinoJson.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <SupportInfo.h>

#include <memory>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DeviceCapabilities.h"
#include "FilenameFontSystem.h"
#include "HyphenationPackStore.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "SupportInfoExport.h"
#include "WifiCredentialStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/home/BookActions.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/home/RecentBookProgress.h"
#include "activities/library/LibraryActivity.h"
#include "activities/network/StatsUploadActivity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/BookStatsActivity.h"
#include "activities/reader/BookStatsView.h"
#include "activities/reader/EpubReaderActivity.h"
#include "activities/reader/EpubReaderDrawerActivity.h"
#include "activities/reader/EpubReaderFootnotesActivity.h"
#include "activities/reader/KOReaderSyncActivity.h"
#include "activities/reader/ReaderFontLoading.h"
#include "activities/reader/ReaderUtils.h"
#include "activities/reader/SideButtonShortcuts.h"
#include "activities/reader/TxtReaderActivity.h"
#include "activities/reader/XtcReaderActivity.h"
#include "activities/settings/AboutActivity.h"
#include "activities/settings/FontSelectionActivity.h"
#include "activities/settings/FrontlightTimePickerActivity.h"
#include "activities/settings/HyphenationManagerActivity.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/settings/QuickActionsActivity.h"
#include "activities/settings/ScreenCalibrationActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/FrontlightPanelActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/CompactHeader.h"
#include "components/HeaderDate.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "simulator/SimulatorHomeKeyInput.h"
#include "util/ButtonShortcutController.h"
#include "util/Dictionary.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

// Use a detached reader so these boundary checks cannot race its render task.
struct EpubReaderCompletionSmokeTest {
  static std::optional<uint32_t> statusAnchor;
  static bool openStatusSettings(EpubReaderActivity& reader) {
    Section* original = nullptr;
    const bool firstEdit = !statusAnchor;
    {
      RenderLock lock;
      if (!reader.section || reader.section->isBuilding()) return false;
      original = reader.section.get();
      if (!statusAnchor && reader.section->pageCount > 1) reader.section->currentPage = 1;
      if (!statusAnchor) statusAnchor = reader.section->getVisibleTextOffsetForPage(reader.section->currentPage);
      if (!statusAnchor) return false;
      SETTINGS.displayStatusBarTextSize = 2;
    }
    reader.endGlobalSettingsEdit();
    if (reader.section.get() != original) return false;  // Global font cannot repaginate the book.
    if (firstEdit) {
      // Two settings changes before the chapter can rebuild must retain the
      // first content anchor even while there is no current Section.
      const bool originallyHidden = SETTINGS.bottomReaderStatusBar.hidden;
      for (const bool hidden : {!originallyHidden, originallyHidden}) {
        {
          RenderLock lock;
          SETTINGS.bottomReaderStatusBar.hidden = hidden;
        }
        reader.endGlobalSettingsEdit();
        if (reader.cachedVisibleTextOffset != statusAnchor || reader.statusBarRelayoutOffset != statusAnchor)
          return false;
      }
    }
    reader.onReaderMenuConfirm(EpubReaderMenuAction::STATUS_BAR_SETTINGS, false);
    return true;
  }
  static bool statusSettingsReturned(EpubReaderActivity& reader) {
    RenderLock lock;
    if (!reader.section || reader.pendingRelayoutReposition) return false;
    const auto page = reader.section->currentPage;
    const auto start = reader.section->getVisibleTextOffsetForPage(page);
    const auto next =
        page + 1 < reader.section->pageCount ? reader.section->getVisibleTextOffsetForPage(page + 1) : std::nullopt;
    return !statusAnchor || (start && *start <= *statusAnchor && (!next || *next > *statusAnchor));
  }
  static bool run(EpubReaderActivity& active) {
    auto reader = std::make_unique<EpubReaderActivity>(renderer, mappedInputManager, nullptr,
                                                       EpubReaderActivity::BookReaderSettingsData{}, 1);
    reader->epub = active.epub;
    const int spineCount = reader->epub->getSpineItemsCount();
    if (spineCount <= 0) return false;

    // Both shortcut entry points must tolerate a chapter that has not loaded yet.
    for (const auto action :
         {CrossPointSettings::SHORT_PWRBTN::PAGE_TURN, CrossPointSettings::SHORT_PWRBTN::PREVIOUS_PAGE}) {
      reader->handleShortcutAction(action);
      reader->handleShortcutAction(static_cast<uint8_t>(action));
      if (reader->section || reader->currentSpineIndex != 0) return false;
    }
    reader->pageTurn(true, "auto");
    reader->pageTurn(false, "test");

    // Previous-page shortcuts on the plain end screen return to the final page.
    for (const bool homeButton : {false, true}) {
      reader->currentSpineIndex = spineCount;
      reader->pendingPageJump = 0;
      if (homeButton) {
        reader->handleShortcutAction(static_cast<uint8_t>(CrossPointSettings::SHORT_PWRBTN::PREVIOUS_PAGE));
      } else {
        reader->handleShortcutAction(CrossPointSettings::SHORT_PWRBTN::PREVIOUS_PAGE);
      }
      if (reader->currentSpineIndex != spineCount - 1 || reader->nextPageNumber != 0 ||
          reader->pendingPageJump != std::numeric_limits<uint16_t>::max())
        return false;
    }
    reader->currentSpineIndex = spineCount;
    reader->handleShortcutAction(CrossPointSettings::SHORT_PWRBTN::PAGE_TURN);
    LOG_INF("SMOKE", "EPUB completion shortcuts: missing chapter and end-screen return passed");
    return true;
  }
};
std::optional<uint32_t> EpubReaderCompletionSmokeTest::statusAnchor;

// Detached activities exercise real popup callbacks and fixed/flowing readers
// while the smoke runner holds the render mutex.
struct StatusBarFeatureSmokeTest {
  using Capture = std::function<void(const std::string&)>;
  static bool open(StatusBarSettingsActivity& editor) {
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouchHardware()) {
      freeink::ui::Interaction hit;
      const int x = renderer.getScreenWidth() / 2;
      for (int y = 0; y < renderer.getScreenHeight(); y += 4) {
        if (!editor.app.hitPublished(x, y, StatusBarSettingsActivity::ACTION_ROW, hit) ||
            hit.value != editor.selectedIndex)
          continue;
        const int tapY = hit.rect.y + hit.rect.height / 2;
        mappedInputManager.simulatorInjectTouchDown(x, tapY);
        editor.loop();
        mappedInputManager.simulatorClearInputFrame();
        mappedInputManager.simulatorInjectTouchRelease(x, tapY);
        editor.loop();
        mappedInputManager.simulatorClearInputFrame();
        return editor.optionPopup.isActive();
      }
      return false;
    }
#endif
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
    editor.loop();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    editor.loop();
    mappedInputManager.simulatorClearInputFrame();
    return editor.optionPopup.isActive();
  }
  static bool choose(StatusBarSettingsActivity& editor, int index, RenderLock& lock) {
    editor.render(std::move(lock));
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouchHardware()) {
      const auto hit = editor.optionPopup.simulatorOptionRect(index);
      mappedInputManager.simulatorInjectTouchDown(hit.x + hit.width / 2, hit.y + hit.height / 2);
      editor.loop();
      mappedInputManager.simulatorClearInputFrame();
      mappedInputManager.simulatorInjectTouchRelease(hit.x + hit.width / 2, hit.y + hit.height / 2);
      editor.loop();
      mappedInputManager.simulatorClearInputFrame();
      return !editor.optionPopup.isActive();
    }
#endif
    // Start from the current option, and move through the real menu mapping.
    for (int i = 0; i < index; ++i) {
      mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      editor.loop();
      mappedInputManager.simulatorClearInputFrame();
    }
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Confirm);
    editor.loop();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Confirm);
    editor.loop();
    mappedInputManager.simulatorClearInputFrame();
    return !editor.optionPopup.isActive();
  }
  static bool settings(RenderLock& lock, const Capture& capture) {
    SETTINGS.legacyXtcTopUsesBottom = 1;
    const auto top = SETTINGS.topReaderStatusBar.slots;
    const auto bottom = SETTINGS.bottomReaderStatusBar.slots;
    const auto language = I18N.getLanguage();
    for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                   GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise}) {
      renderer.setOrientation(orientation);
      for (int bar = 0; bar < 2; ++bar) {
        StatusBarSettingsActivity editor(renderer, mappedInputManager, true);
        editor.onEnter();
        editor.selectedIndex = bar;
        editor.handleSelection();
        editor.render(std::move(lock));
        // Reach the final Hide row through actual button navigation, including
        // the variable-height list's scrolling and section boundaries.
        for (int i = 0; i < 11; ++i) {
          mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
          editor.loop();
          mappedInputManager.simulatorClearInputFrame();
          editor.render(std::move(lock));
        }
        if (editor.selectedIndex != 11 || !editor.simulatorSelectedRowVisible) return false;
        for (int repeat = 0; repeat < 4; ++repeat) {
          const auto position = bar ? ReaderStatusBarPosition::Bottom : ReaderStatusBarPosition::Top;
          const bool before = SETTINGS.readerStatusBar(position).hidden;
          if (!open(editor)) return false;
          editor.render(std::move(lock));
          if (repeat == 0)
            capture("hide-picker-lang-" + std::to_string(static_cast<int>(language)) + "-o-" +
                    std::to_string(orientation) + "-bar-" + std::to_string(bar));
          if (!choose(editor, mappedInputManager.hasTouchHardware() ? !before : 1, lock)) return false;
          if (SETTINGS.readerStatusBar(position).hidden == before || SETTINGS.topReaderStatusBar.slots != top ||
              SETTINGS.bottomReaderStatusBar.slots != bottom || SETTINGS.legacyXtcTopUsesBottom != 1)
            return false;
          editor.render(std::move(lock));
          editor.render(std::move(lock));  // Closing a popup first restores its saved framebuffer.
          if (!editor.simulatorSelectedRowVisible) return false;
          if (repeat == 0)
            capture("hide-row-lang-" + std::to_string(static_cast<int>(language)) + "-o-" +
                    std::to_string(orientation) + "-bar-" + std::to_string(bar));
        }
        editor.onExit();
      }
    }

    for (uint8_t size = 0; size < 3; ++size) {
      SETTINGS.displayStatusBarTextSize = 0;
      const auto readerSize = SETTINGS.statusBarTextSize;
      StatusBarSettingsActivity editor(renderer, mappedInputManager, false, false, true);
      editor.onEnter();
      editor.render(std::move(lock));
      for (int i = 0; i < 4; ++i) {
        mappedInputManager.simulatorInjectRelease(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        editor.loop();
        mappedInputManager.simulatorClearInputFrame();
        editor.render(std::move(lock));
      }
      if (editor.selectedIndex != 4 || !editor.simulatorSelectedRowVisible) return false;
      if (!open(editor)) return false;
      if (!choose(editor, size, lock) || SETTINGS.displayStatusBarTextSize != size ||
          SETTINGS.statusBarTextSize != readerSize)
        return false;
      editor.render(std::move(lock));
      editor.render(std::move(lock));
      capture("global-size-editor-" + std::to_string(size));
      editor.onExit();
    }
    // Battery styles are per bar: changing one must leave the others untouched.
    const auto displayBattery = SETTINGS.displayStatusBar.batteryStyle;
    const auto readerTopBattery = SETTINGS.topReaderStatusBar.batteryStyle;
    const auto readerBottomBattery = SETTINGS.bottomReaderStatusBar.batteryStyle;
    for (int style = 2; style >= 0; --style) {
      SETTINGS.displayStatusBar.batteryStyle = ReaderStatusBarBatteryStyle::IconAndPercent;
      StatusBarSettingsActivity editor(renderer, mappedInputManager, false, false, true);
      editor.onEnter();
      editor.selectedIndex = 3;
      editor.render(std::move(lock));
      if (!open(editor) || !choose(editor, style, lock) ||
          SETTINGS.displayStatusBar.batteryStyle != static_cast<ReaderStatusBarBatteryStyle>(style) ||
          SETTINGS.topReaderStatusBar.batteryStyle != readerTopBattery ||
          SETTINGS.bottomReaderStatusBar.batteryStyle != readerBottomBattery)
        return false;
      editor.render(std::move(lock));
      editor.render(std::move(lock));
      capture("global-battery-editor-" + std::to_string(style));
      editor.onExit();
    }
    for (int style = 2; style >= 0; --style) {
      SETTINGS.bottomReaderStatusBar.batteryStyle = ReaderStatusBarBatteryStyle::IconAndPercent;
      StatusBarSettingsActivity editor(renderer, mappedInputManager, true);
      editor.onEnter();
      editor.selectedIndex = 1;
      editor.handleSelection();
      editor.selectedIndex = 7;
      editor.render(std::move(lock));
      if (!open(editor) || !choose(editor, style, lock) ||
          SETTINGS.bottomReaderStatusBar.batteryStyle != static_cast<ReaderStatusBarBatteryStyle>(style) ||
          SETTINGS.topReaderStatusBar.batteryStyle != readerTopBattery)
        return false;
      editor.render(std::move(lock));
      editor.render(std::move(lock));
      capture("bottom-battery-editor-" + std::to_string(style));
      editor.onExit();
    }
    SETTINGS.displayStatusBar.batteryStyle = displayBattery;
    SETTINGS.bottomReaderStatusBar.batteryStyle = readerBottomBattery;
    return true;
  }
  static bool txt(RenderLock& lock, const Capture& capture) {
    const char* path = "/books/status-feature.txt";
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return false;
    for (int i = 0; i < 1200; ++i) {
      const auto line =
          "Paragraph " + std::to_string(i) + " preserves the reading position through status bar edits.\n";
      if (file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size()) != line.size()) return false;
    }
    file.close();
    auto book = std::make_unique<Txt>(path, "/.crosspoint");
    if (!book->load()) return false;
    book->setupCacheDir();
    TxtReaderActivity reader(renderer, mappedInputManager, std::move(book), 1);
    SETTINGS.topReaderStatusBar.hidden = false;
    SETTINGS.bottomReaderStatusBar.hidden = false;
    reader.render(std::move(lock));
    reader.currentPage = std::min(4, reader.totalPages - 1);
    const auto originalOffset = reader.pageOffsets[reader.currentPage];
    for (const unsigned mask : {0, 1, 3, 2, 0, 3, 0}) {
      const auto offset = originalOffset;
      SETTINGS.topReaderStatusBar.hidden = mask & 1;
      SETTINGS.bottomReaderStatusBar.hidden = mask & 2;
      reader.render(std::move(lock));
      const auto next = reader.currentPage + 1;
      if (reader.pageOffsets[reader.currentPage] > offset ||
          (next < reader.totalPages && reader.pageOffsets[next] <= offset))
        return false;
      if ((mask & 1) && reader.cachedTopStatusBarHeight != 0) return false;
      if ((mask & 2) && reader.cachedFooterReservedHeight != SETTINGS.screenMarginVertical) return false;
      capture("txt-hidden-" + std::to_string(mask));
    }
    if (!reader.saveProgress(reader.currentPage)) return false;
    auto reopened = std::make_unique<Txt>(path, "/.crosspoint");
    if (!reopened->load()) return false;
    TxtReaderActivity second(renderer, mappedInputManager, std::move(reopened), 1);
    second.render(std::move(lock));
    if (second.currentPage != reader.currentPage || second.pageOffsets != reader.pageOffsets) return false;

    // A page turn starts a new anchor; changing bars while the book is closed
    // restores the saved content offset rather than a now-obsolete page number.
    second.currentPage = std::min(second.currentPage + 2, second.totalPages - 1);
    second.render(std::move(lock));
    const auto turnedOffset = second.pageOffsets[second.currentPage];
    if (!second.saveProgress(second.currentPage)) return false;
    SETTINGS.topReaderStatusBar.hidden = true;
    SETTINGS.bottomReaderStatusBar.hidden = true;
    auto closedBook = std::make_unique<Txt>(path, "/.crosspoint");
    if (!closedBook->load()) return false;
    TxtReaderActivity third(renderer, mappedInputManager, std::move(closedBook), 1);
    third.render(std::move(lock));
    const auto contains = [](const TxtReaderActivity& activity, size_t offset) {
      return activity.pageOffsets[activity.currentPage] <= offset &&
             (activity.currentPage + 1 == activity.totalPages ||
              activity.pageOffsets[activity.currentPage + 1] > offset);
    };
    if (!contains(third, turnedOffset)) return false;
    SETTINGS.topReaderStatusBar.hidden = false;
    SETTINGS.bottomReaderStatusBar.hidden = false;
    second.render(std::move(lock));
    if (!contains(second, turnedOffset)) return false;

    // Preserve legacy page-only progress and tolerate an invalid modern offset.
    const auto progressPath = third.txt->getCachePath() + "/progress.bin";
    for (const size_t bytes : {2, 4, 6}) {
      const uint8_t record[6] = {2, 0, 255, 255, 255, 255};
      auto progress = Storage.open(progressPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
      if (!progress || progress.write(record, bytes) != bytes) return false;
      progress.close();
      third.loadProgress();
      if (third.currentPage != 2) return false;
    }
    const uint8_t invalidZero[6] = {2, 0, 0, 0, 0, 0};
    auto zeroProgress = Storage.open(progressPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!zeroProgress || zeroProgress.write(invalidZero, sizeof(invalidZero)) != sizeof(invalidZero)) return false;
    zeroProgress.close();
    third.loadProgress();
    if (third.currentPage != 2) return false;
    // Exiting during a font rebuild must not publish a made-up byte offset.
    const int pendingPage = third.currentPage;
    third.pageOffsets.clear();
    if (!third.saveProgress(pendingPage)) return false;
    auto progress = Storage.open(progressPath.c_str(), O_RDONLY);
    uint8_t pendingRecord[6]{};
    if (!progress || progress.size() != sizeof(pendingRecord) ||
        progress.read(pendingRecord, sizeof(pendingRecord)) != sizeof(pendingRecord))
      return false;
    progress.close();
    if (pendingRecord[0] != pendingPage || pendingRecord[1] != 0 || pendingRecord[2] != 255 ||
        pendingRecord[3] != 255 || pendingRecord[4] != 255 || pendingRecord[5] != 255)
      return false;
    return true;
  }
  static bool xtc(RenderLock& lock, const Capture& capture) {
    // A real fixed-layout bitmap with black baked-in strips and a central line.
    const uint16_t width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
    const uint32_t bytes = ((width + 7) / 8) * height;
    xtc::XtcHeader header{};
    header.magic = xtc::XTC_MAGIC;
    header.versionMajor = 1;
    header.pageCount = 1;
    header.pageTableOffset = sizeof(header);
    header.dataOffset = sizeof(header) + sizeof(xtc::PageTableEntry);
    const xtc::PageTableEntry entry{header.dataOffset, static_cast<uint32_t>(sizeof(xtc::XtgPageHeader)) + bytes, width,
                                    height};
    const xtc::XtgPageHeader page{xtc::XTG_MAGIC, width, height, 0, 0, bytes, 0};
    auto file = Storage.open("/books/status-feature.xtc", O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return false;
    file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header));
    file.write(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry));
    file.write(reinterpret_cast<const uint8_t*>(&page), sizeof(page));
    std::vector<uint8_t> pixels(bytes, 0xff);
    const size_t stride = (width + 7) / 8;
    for (int y = 0; y < height; ++y) {
      if (y < 18 || y >= height - 18 || y == height / 2) std::fill_n(pixels.data() + y * stride, stride, 0);
    }
    if (file.write(pixels.data(), pixels.size()) != pixels.size()) return false;
    file.close();
    auto book = std::make_unique<Xtc>("/books/status-feature.xtc", "/.crosspoint");
    if (!book->load()) return false;
    XtcReaderActivity reader(renderer, mappedInputManager, std::move(book), 1);
    SETTINGS.xtcStatusBarMode = CrossPointSettings::XTC_STATUS_BAR_BOTH;
    SETTINGS.legacyXtcTopUsesBottom = 1;
    for (const unsigned mask : {0, 1, 3, 2, 0}) {
      SETTINGS.topReaderStatusBar.hidden = mask & 1;
      SETTINGS.bottomReaderStatusBar.hidden = mask & 2;
      reader.renderPage(0);
      if (reader.currentPage != 0 || !renderer.isPixelBlack(width / 2, height / 2)) return false;
      int marginTop, marginRight, marginBottom, marginLeft;
      renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
      // Sample the visible strips, beyond the existing physical panel gutter.
      if (mask == 3 && (renderer.isPixelBlack(width / 2, marginTop + 1) ||
                        renderer.isPixelBlack(width / 2, height - marginBottom - 1)))
        return false;
      capture("xtc-legacy-hidden-" + std::to_string(mask));
    }
    return true;
  }
};

struct ScreenCalibrationSmokeTest {
  static bool geometry(RenderLock& lock, const StatusBarFeatureSmokeTest::Capture& capture) {
    const auto original = renderer.getViewableInsets();
    const auto orientation = renderer.getOrientation();
    const auto theme = SETTINGS.uiTheme;
    const auto topStatus = SETTINGS.topReaderStatusBar;
    SETTINGS.topReaderStatusBar = ReaderStatusBarConfig{};
    SETTINGS.topReaderStatusBar.progressBar = CrossPointSettings::BOOK_PROGRESS;
    renderer.setViewableInsets(ScreenInsets{{{32, 32, 32, 32}}});
    // Heap-owned fixture/activity are simulator-only; never add their large
    // activity state to a firmware task stack.
    std::vector<FootnoteEntry> entries(2);
    std::strcpy(entries[0].number, "1. Calibration first footnote");
    std::strcpy(entries[1].number, "2. Calibration second footnote");
    bool valid = true;
    for (const auto variant :
         {CrossPointSettings::CLASSIC, CrossPointSettings::LYRA, CrossPointSettings::ROUNDEDRAFF}) {
      SETTINGS.uiTheme = variant;
      UITheme::getInstance().reload();
      for (unsigned rotation = 0; rotation < 4; ++rotation) {
        renderer.setOrientation(static_cast<GfxRenderer::Orientation>(rotation));
        auto list = std::make_unique<EpubReaderFootnotesActivity>(renderer, mappedInputManager, entries);
        list->onEnter();
        list->render(std::move(lock));
        const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
        const auto header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager, safe);
        const auto back = TouchHeaderBackButton::layout(header);
        const int statusY = UITheme::getTopStatusBarY(renderer);
        const auto& metrics = UITheme::getInstance().getMetrics();
        valid &= header.x >= 32 && header.x + header.width <= renderer.getScreenWidth() - 32 && header.y >= 32;
        const int displayY = std::max(32, header.y + UITheme::getTopStatusBarInset(renderer));
        const int iconTop = back.iconRect.y + (back.iconRect.height - TouchHeaderBackButton::ICON_SIZE) / 2 +
                            TouchHeaderBackButton::TITLE_VERTICAL_OFFSET;
        valid &= statusY >= 32 &&
                 displayY + UITheme::getDisplayStatusBarTextHeight(renderer) + ReaderStatusBarConfig::TOP_TEXT_INSET <=
                     iconTop;
        valid &= back.touchRect.x >= 32 && back.touchRect.y >= 32;
        valid &= ReaderUtils::getTopStatusBarReservedHeight(renderer) ==
                 std::max(0, statusY + UITheme::getReaderStatusBarHeight(ReaderStatusBarPosition::Top, renderer) - 32);
        const int rowHeight = uiListRowHeight(list->app.theme(), UiListRowType::SingleLine);
        const int expectedHitY = TouchHeaderBackButton::contentTop(renderer, mappedInputManager, safe.y) +
                                 metrics.verticalSpacing - std::max(0, list->app.device().minTouchSize - rowHeight) / 2;
        freeink::ui::Interaction hit;
        bool found = false;
        for (int y = header.y + header.height; y < renderer.getScreenHeight() - 32; ++y) {
          if (!list->app.hitPublished(renderer.getScreenWidth() / 2, y, 1, hit) || hit.value != 0) continue;
          found = true;
          valid &= hit.rect.x == safe.x + list->app.theme().listInset;
          valid &= hit.rect.y == expectedHitY;
          break;
        }
        valid &= found;
        LOG_INF("SMOKE",
                "Geometry theme=%d rotation=%u header=%d,%d,%d,%d status=%d+%d iconY=%d back=%d,%d first=%d,%d "
                "expected=%d,%d found=%d",
                static_cast<int>(variant), rotation, header.x, header.y, header.width, header.height, statusY,
                metrics.batteryBarHeight,
                back.iconRect.y + (back.iconRect.height - TouchHeaderBackButton::ICON_SIZE) / 2, back.touchRect.x,
                back.touchRect.y, hit.rect.x, hit.rect.y, safe.x + list->app.theme().listInset, expectedHitY, found);
#if CROSSINK_APP_CAP_TOUCH
        if (mappedInputManager.hasTouchHardware()) {
          const int x = back.touchRect.x + back.touchRect.width / 2;
          const int y = back.touchRect.y + back.touchRect.height / 2;
          mappedInputManager.simulatorInjectTouchDown(x, y);
          mappedInputManager.simulatorClearInputFrame();
          mappedInputManager.simulatorInjectTouchRelease(x, y);
          const bool tapped = TouchHeaderBackButton::wasTapped(mappedInputManager, header);
          LOG_INF("SMOKE", "Geometry tap=%d statusBottom=%d iconTop=%d reservation=%d", tapped,
                  displayY + UITheme::getDisplayStatusBarTextHeight(renderer) + ReaderStatusBarConfig::TOP_TEXT_INSET,
                  iconTop, ReaderUtils::getTopStatusBarReservedHeight(renderer));
          valid &= tapped;
          mappedInputManager.simulatorClearInputFrame();
        }
#endif
        capture("calibration-max32-theme-" + std::to_string(static_cast<int>(variant)) + "-rotation-" +
                std::to_string(rotation));
        renderer.clearScreen();
        ReaderStatusBarContent progress;
        progress.bookProgress = 50;
        progress.showProgress = true;
        GUI.drawReaderStatusBar(renderer, ReaderStatusBarPosition::Top, progress);
        valid &= renderer.isPixelBlack(renderer.getScreenWidth() / 4, statusY);
        valid &= !renderer.isPixelBlack(renderer.getScreenWidth() / 4, 31);
        capture("calibration-reader-progress-theme-" + std::to_string(static_cast<int>(variant)) + "-rotation-" +
                std::to_string(rotation));
        if (rotation == 0) {
          renderer.setViewableInsets(ScreenInsets{});
          list->render(std::move(lock));
          const auto restored = list->app.device().safeRect();
          valid &= restored.x == 3 && restored.y == 9;
          capture("calibration-suspended-menu-reset-theme-" + std::to_string(static_cast<int>(variant)));
          renderer.setViewableInsets(ScreenInsets{{{32, 32, 32, 32}}});
        }
        list->onExit();
      }
    }
    renderer.setViewableInsets(original);
    renderer.setOrientation(orientation);
    SETTINGS.uiTheme = theme;
    SETTINGS.topReaderStatusBar = topStatus;
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Calibration max32 headers/back targets/list geometry: %s", valid ? "passed" : "failed");
    return valid;
  }
  static bool keyboardAndHints(RenderLock& lock, const StatusBarFeatureSmokeTest::Capture& capture) {
    const auto original = renderer.getViewableInsets();
    const auto orientation = renderer.getOrientation();
    const auto theme = SETTINGS.uiTheme;
    bool valid = true;
    for (const auto variant : {CrossPointSettings::CLASSIC, CrossPointSettings::LYRA, CrossPointSettings::ROUNDEDRAFF,
                               CrossPointSettings::MINIMAL}) {
      SETTINGS.uiTheme = variant;
      UITheme::getInstance().reload();
      for (const bool custom : {false, true}) {
        renderer.setViewableInsets(custom ? ScreenInsets{{{32, 32, 32, 32}}} : ScreenInsets{});
        const int reserve = UITheme::getButtonHintsReserve(renderer);
        valid &= reserve == (mappedInputManager.hasTouchHardware() ? 0 : custom ? 72 : 40);
        for (unsigned rotation = 0; rotation < 4; ++rotation) {
          renderer.setOrientation(static_cast<GfxRenderer::Orientation>(rotation));
          renderer.clearScreen();
          GUI.drawButtonHints(renderer, "Back", "OK", "Up", "Down", true);
          GUI.drawSideButtonHints(renderer, ">", "<");
          if (custom) {
            const int width = renderer.getScreenWidth(), height = renderer.getScreenHeight();
            // Outline endpoints use renderer's inclusive rectangle convention.
            // Every glyph and outline must be within these physical boundaries.
            for (int y = 0; y < height; ++y)
              for (int x = 0; x < width; ++x)
                if (renderer.isPixelBlack(x, y) && (x < 32 || x > width - 32 || y < 32 || y > height - 32))
                  valid = false;
            const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
            if (!mappedInputManager.hasTouchHardware()) {
              if (rotation == 0) valid &= safe.y + safe.height == height - reserve;
              if (rotation == 1) valid &= safe.x == reserve;
              if (rotation == 2) valid &= safe.y == reserve;
              if (rotation == 3) valid &= safe.x + safe.width == width - reserve;
            }
          }
          capture("calibration-hints-theme-" + std::to_string(static_cast<int>(variant)) +
                  (custom ? "-max32-rotation-" : "-default-rotation-") + std::to_string(rotation));
        }
      }
    }
    SETTINGS.uiTheme = theme;
    UITheme::getInstance().reload();
    renderer.setOrientation(GfxRenderer::Portrait);
    renderer.setViewableInsets(ScreenInsets{{{32, 32, 32, 32}}});
    auto keyboard =
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInputManager, "Password", "", 64, InputType::Password);
    keyboard->onEnter();
    keyboard->render(std::move(lock));
    const auto keys = keyboard->keyboardRect();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    valid &= keys.x >= 32 && keys.right() <= renderer.getScreenWidth() - 32 && keys.bottom() <= safe.y + safe.height;
    valid &= keyboard->textFieldMargin() >= 32;
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int fieldBottom = TouchHeaderBackButton::contentTop(renderer, mappedInputManager) +
                            metrics.verticalSpacing * 5 + metrics.keyboardVerticalOffset + keyboard->inputLineHeight;
    for (int y = fieldBottom; y < keys.y; ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x)
        if ((x < 32 || x > renderer.getScreenWidth() - 32) && renderer.isPixelBlack(x, y)) valid = false;
    bool firstLetter = false, lastLetter = false, mode = false, ok = false;
    for (unsigned i = 0; i < keyboard->interactions.publishedCount(); ++i) {
      const auto& hit = keyboard->interactions.publishedData()[i];
      valid &= hit.rect.x >= 32 && hit.rect.right() <= renderer.getScreenWidth() - 32;
      valid &= hit.rect.bottom() <= safe.y + safe.height;
      const int16_t value = hit.value;
      firstLetter |= value == 'q';
      lastLetter |= value == 'p';
      mode |= value == freeink::ui::QWERTY_KEY_MODE;
      ok |= value == freeink::ui::QWERTY_KEY_ENTER;
#if CROSSINK_APP_CAP_TOUCH
      if (mappedInputManager.hasTouchHardware() && (value == 'q' || value == 'p')) {
        mappedInputManager.simulatorInjectTouchDown(hit.rect.x + hit.rect.width / 2, hit.rect.y + hit.rect.height / 2);
        keyboard->loop();
        mappedInputManager.simulatorClearInputFrame();
        mappedInputManager.simulatorInjectTouchRelease(hit.rect.x + hit.rect.width / 2,
                                                       hit.rect.y + hit.rect.height / 2);
        keyboard->loop();
        mappedInputManager.simulatorClearInputFrame();
      }
#endif
    }
    valid &= firstLetter && lastLetter && mode && ok;
    if (mappedInputManager.hasTouchHardware())
      valid &= keyboard->text.find('q') != std::string::npos && keyboard->text.find('p') != std::string::npos;
    capture("calibration-password-keyboard-max32");
    keyboard->onExit();
    auto fonts = std::make_unique<FontSelectionActivity>(renderer, mappedInputManager, nullptr);
    fonts->updateLayoutMetrics();
    const int customTop = fonts->afterHeader;
    const int customHeight = fonts->usableHeight;
    renderer.setViewableInsets(ScreenInsets{});
    fonts->updateLayoutMetrics();
    valid &= fonts->afterHeader < customTop && fonts->usableHeight > customHeight;
    renderer.setViewableInsets(original);
    renderer.setOrientation(orientation);
    LOG_INF("SMOKE", "Calibration front/side hints, keyboard edge keys, font picker resume: %s",
            valid ? "passed" : "failed");
    return valid;
  }
  static bool timePickerHit(const FrontlightTimePickerActivity& picker, const int16_t value,
                            freeink::ui::Interaction& hit) {
    bool found = false;
    for (unsigned i = 0; i < picker.keyboardInteractions.publishedCount(); ++i) {
      const auto& key = picker.keyboardInteractions.publishedData()[i];
      if (key.rect.x < 32 || key.rect.y < 32 || key.rect.right() > renderer.getScreenWidth() - 32 ||
          key.rect.bottom() > renderer.getScreenHeight() - 32)
        return false;
      if (key.value == value) {
        hit = key;
        found = true;
      }
    }
    // Include the fixed time fields and backspace artwork, which do not all
    // publish active touch targets until a numeric entry is pending.
    for (int y = 0; y < renderer.getScreenHeight(); ++y)
      for (int x = 0; x < renderer.getScreenWidth(); ++x)
        if (renderer.isPixelBlack(x, y) &&
            (x < 32 || y < 32 || x > renderer.getScreenWidth() - 32 || y > renderer.getScreenHeight() - 32))
          return false;
    return found;
  }
  static bool timePickerHourIsTwelve(const FrontlightTimePickerActivity& picker) { return picker.hour12 == 12; }
  static bool txt(RenderLock& lock) {
    const char* path = "/books/calibration.txt";
    auto file = Storage.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!file) return false;
    constexpr char line[] = "Screen calibration keeps this paragraph visible and preserves its reading position.\n";
    for (int i = 0; i < 600; ++i)
      if (file.write(reinterpret_cast<const uint8_t*>(line), sizeof(line) - 1) != sizeof(line) - 1) return false;
    file.close();
    auto book = std::make_unique<Txt>(path, "/.crosspoint");
    if (!book->load()) return false;
    book->setupCacheDir();
    TxtReaderActivity reader(renderer, mappedInputManager, std::move(book), 1);
    reader.render(std::move(lock));
    if (reader.totalPages < 3) return false;
    reader.currentPage = 2;
    const auto offset = reader.pageOffsets[reader.currentPage];
    const auto original = renderer.getViewableInsets();
    auto changed = original;
    changed.adjust(1, 1);
    changed.adjust(3, 1);
    const int width = reader.viewportWidth;
    renderer.setViewableInsets(changed);
    reader.render(std::move(lock));
    if (reader.cachedViewableInsets != changed || reader.viewportWidth != width - 2) return false;
    const int left = reader.cachedOrientedMarginLeft;
    changed.adjust(3, 1);
    changed.adjust(1, -1);
    renderer.setViewableInsets(changed);
    reader.render(std::move(lock));
    if (reader.viewportWidth != width - 2 || reader.cachedOrientedMarginLeft != left + 1 ||
        reader.pageOffsets[reader.currentPage] > offset ||
        (reader.currentPage + 1 < reader.totalPages && reader.pageOffsets[reader.currentPage + 1] <= offset))
      return false;
    renderer.setViewableInsets(original);
    return true;
  }
  static std::optional<uint32_t> epubOffset;
  static int epubWidth;
  static bool epubReady(const EpubReaderActivity& reader) {
    return reader.section && !reader.section->isBuilding() && !reader.pendingRelayoutReposition;
  }
  static bool reflowEpub(EpubReaderActivity& reader) {
    {
      RenderLock lock;
      reader.section->currentPage = std::min(1, reader.section->pageCount - 1);
      epubOffset = reader.section->getVisibleTextOffsetForPage(reader.section->currentPage);
      epubWidth = reader.buildViewportWidth;
      auto changed = renderer.getViewableInsets();
      changed.adjust(1, 1);
      changed.adjust(3, 1);
      renderer.setViewableInsets(changed);
    }
    reader.endGlobalSettingsEdit();
    RenderLock lock;
    return !reader.section && reader.cachedVisibleTextOffset == epubOffset;
  }
  static bool reflowedEpub(const EpubReaderActivity& reader) {
    const int page = reader.section->currentPage;
    const auto start = reader.section->getVisibleTextOffsetForPage(page);
    const auto next =
        page + 1 < reader.section->pageCount ? reader.section->getVisibleTextOffsetForPage(page + 1) : std::nullopt;
    return reader.buildViewportWidth == epubWidth - 2 && epubOffset && start && *start <= *epubOffset &&
           (!next || *next > *epubOffset);
  }
};
std::optional<uint32_t> ScreenCalibrationSmokeTest::epubOffset;
int ScreenCalibrationSmokeTest::epubWidth = 0;

namespace {

enum class SmokeStep : uint8_t {
  Start,
  HomeReaderReader,
  HomeReaderNested,
  HomeReaderConfirmation,
  HomeReaderTrigger,
  HomeReaderUnwound,
  HomeReaderReturnedHome,
  HomeReaderNonReaderParent,
  HomeReaderNonReaderNested,
  HomeReaderNonReaderHome,
  BackHomeReader,
  BackHomeNested,
  BackHomeReturnedReader,
  BackHomeReturnedHome,
  NavigationLongReader,
  NavigationLongHeld,
  NavigationLongReleased,
  NavigationPicker,
  Home,
  FileBrowser,
  FileBrowserSettings,
  Library,
  StatsUploadEntry,
  SyncServerCaptureBottom,
  SyncServerCaptureUnsupported,
  SyncServerCaptureStats,
  StatsUploadReturn,
  StatsUploadEmptyDone,
  RecentLibrary,
  Settings,
  SideButtons,
  ReaderMenu,
  Sleep,
  Reader,
  ReaderInput,
  CompletionReturnedHome,
  CompletionReaderRestored,
  StatusBarReaderSettings,
  StatusBarReaderReturned,
  CarouselHome,
  FrontlightLayout,
  FrontlightLayoutRendered,
  ThemeHome,
  ThemeSettings,
  ThemeReturned,
  ThemeFresh,
  StatusBarEditor,
  StatusBarPicker,
  Done,
};

class HomeReaderSmokeActivity final : public Activity {
 public:
  HomeReaderSmokeActivity(const char* activityName, const bool reader, GfxRenderer& renderer,
                          MappedInputManager& mappedInput, const bool bookReader = false)
      : Activity(activityName, renderer, mappedInput), reader(reader), bookReader(bookReader) {}

  bool isReaderActivity() const override { return reader; }
  bool isBookReaderActivity() const override { return bookReader; }

 private:
  bool reader;
  bool bookReader;
};

// Stands in for a reader hosting Reading Stats: its result handler must run
// before Sync All replaces the stack, even when it pushes another screen.
bool statsParentHandlerRan = false;

class StatsParentSmokeActivity final : public Activity {
 public:
  StatsParentSmokeActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("StatsParent", renderer, input) {}

  void onEnter() override {
    Activity::onEnter();
    startActivityForResult(
        std::make_unique<BookStatsActivity>(renderer, mappedInput, "Fixture", std::string{}, BookReadingStats{}, -1.0f,
                                            false, 0, GlobalReadingStats{}),
        [this](const ActivityResult&) {
          // Same as EpubReaderActivity importing edits, then reopening its menu.
          statsParentHandlerRan = activityManager.hasDeferredReplace();
          startActivityForResult(
              std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Pushed by parent", "Pushed by parent"),
              [](const ActivityResult&) {});
        });
  }
  void loop() override {}
  void render(RenderLock&&) override {
    renderer.clearScreen();
    renderer.displayBuffer();
  }
};

class EntryRenderSmokeActivity final : public Activity {
  std::atomic<bool> ready{false};
  std::atomic<unsigned> renders{0};

  static void check(bool passed, const char* message) {
    if (passed) return;
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(1);
  }

 public:
  EntryRenderSmokeActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("EntryRenderSmoke", renderer, input) {}

  void onEnter() override {
    Activity::onEnter();
    requestUpdate(true);
    delay(80);
    check(renders == 0, "Activity rendered before entry state was ready");
    ready = true;
    check(requestUpdateAndWait() == RequestUpdateResult::Rendered, "Entry loading render was rejected");
    check(renders > 0, "Entry loading screen was not rendered");
    ready = false;
    const unsigned previous = renders;
    requestUpdate(true);
    delay(80);
    check(renders == previous, "Asynchronous render ran while entry resumed initialization");
    ready = true;
    requestUpdate();
    LOG_INF("SMOKE", "Entry render isolation and synchronous short-note popup passed");
  }

  void render(RenderLock&&) override {
    check(ready, "Render observed incomplete entry state");
    renderer.clearScreen();
    static const char* const options[] = {"Cancel", "Save"};
    const OptionLabels labels(options, 2,
                              [](const void* owner, size_t i) { return static_cast<const char* const*>(owner)[i]; });
    GUI.drawOptionPopup(renderer, "Popup regression", labels, 0, false, nullptr, nullptr, false, -1,
                        "Note:", "Short note.");
    ++renders;
    renderer.displayBuffer();
  }
};

class NavigationPickerSmokeActivity final : public Activity {
  OptionPopup popup;

 public:
  NavigationPickerSmokeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NavigationPicker", renderer, mappedInput) {}
  void onEnter() override {
    Activity::onEnter();
    const auto setting = buildShortcutSetting(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn, "shortPwrBtn",
                                              ShortcutOptionCatalog::PowerButton);
    std::vector<std::string> labels;
    labels.reserve(setting.enumValues.size());
    for (const auto label : setting.enumValues) labels.emplace_back(I18N.get(label));
    popup.show(StrId::STR_SHORT_PWR_BTN, labels, labels.size() - 2, [](int) {});
    requestUpdate();
  }
  void render(RenderLock&&) override {
    renderer.clearScreen();
    popup.processRender(renderer, mappedInput);
  }
};

class SimulatorSmokeTest {
 public:
  void tick() {
    if (!enabled()) return;

    try {
      tickImpl();
    } catch (const std::exception& e) {
      fail("Unhandled exception: %s", e.what());
    } catch (...) {
      fail("Unhandled non-standard exception");
    }
  }

 private:
  enum class ScriptActionType : uint8_t {
    Press,
    Release,
    HomeTap,
    HomeLongPress,
    ConfigureChapterShortcuts,
    RestoreChapterShortcuts,
    ConfigureChapterHomeDoubleTap,
    WaitForChapterSelection,
    WaitForMenuLongPress,
    ConfigureHomeButtonPowerLock,
    WaitForPowerLongPress,
    AssertHomeButtonDisabled,
    AssertHomeButtonEnabled,
    AssertTouchscreenDisabled,
    AssertTouchscreenEnabled,
    AssertTtfProfileNative,
    OpenSmokeBook,
    OpenFrontlightSettings,
    ClearFrontlightSyncBook,
    PrepareFrontlightSync,
    OpenFrontlightSync,
    CheckFrontlightSync,
    DisableReadingStats,
    EnableReadingStats,
    DisableReaderTouch,
    EnableReaderTouch,
    TouchDown,
    TouchDrawerHandle,
    TouchFrontlightQuickAction,
    TouchMove,
    TouchRelease,
    AssertReaderMenu,
    AssertSettingsNavigation,
    AssertMenuNavigation,
    WaitForSettingsCategory,
    CaptureMenuNavigation,
    AssertAboutTopIndex,
    WaitForNavigationHold,
    AssertActivity,
    Render
  };

  struct ScriptAction {
    ScriptActionType type;
    MappedInputManager::Button button;
    const char* label;
    int settleFrames;
    int x;
    int y;
  };

  uint8_t savedChapterShortcuts[7]{};
  uint32_t lastInjectedHomeAt = 0;
  SmokeStep step = SmokeStep::Start;
  int settleFrames = 0;
  const char* activeStepName = nullptr;
  std::vector<ScriptAction> inputScript;
  size_t scriptIndex = 0;
  unsigned libraryRefreshPass = 0;
  uint16_t libraryBaselineBooks = 0;
  SmokeStep inputCompletionStep = SmokeStep::Done;
  unsigned carouselCachePass = 0;
  std::filesystem::file_time_type carouselCacheWrittenAt;
  std::filesystem::file_time_type carouselSecondWrittenAt;
  uint64_t carouselCacheHash = 0;
  uint64_t carouselScreenHash = 0;
  unsigned frontlightLayoutPass = 0;
  unsigned supportPhase = 0;
  std::string priorSupportExport;
  unsigned filenameFontPhase = 0;
  unsigned hyphenationPhase = 0;
  unsigned aboutPhase = 0;
  unsigned aboutPass = 0;
  uint32_t aboutSnapshotUptime = 0;
  unsigned homeThemePass = 0;
  unsigned calibrationPhase = 0;
  unsigned calibrationPickerPass = 0;
  int calibrationPickerResult = -1;
  ScreenInsets calibrationExpected;
  Activity* calibrationResumedHome = nullptr;
  std::string calibrationStoragePath;
  uint64_t homeThemeScreenHash = 0;
  std::string homeThemeBookPath;

  uint8_t homeReaderReaderKind = 0;
  uint8_t homeReaderCancelledMask = 0;
  Activity* homeReaderSmokeReader = nullptr;
  bool homeReaderConfirmationAccepted = false;
  bool backHomeChildCancelled = false;
  uint8_t navigationLongPass = 0;
  uint8_t savedNavigationMenu = 0;
  uint8_t savedNavigationBack = 0;
  unsigned long navigationPressAt = 0;
  Activity* navigationExpected = nullptr;

  void prepareRecentLibrary() {
    SETTINGS.librarySortMethod = 4;
    SETTINGS.librarySortDescending = 1;
    SETTINGS.libraryUseMetadata = 1;
    SETTINGS.libraryShowTxt = 1;
    SETTINGS.libraryHideFinishedBooks = 0;
    for (int i = 0; i < 20; ++i) {
      const std::string path = "/books/recent-smoke-" + std::to_string(i) + ".txt";
      if (!Storage.writeFile(path.c_str(), "Recent Library smoke fixture")) fail("Cannot create recent fixture");
      RECENT_BOOKS.addOrUpdateBook(path, "Title " + std::to_string(i), "Author " + std::to_string(i), "");
    }
    Storage.remove(library::libraryIndexPath());
    library::invalidateLibraryIndex();
  }

  static const char* homeReaderSmokeReaderName(const uint8_t kind) {
    switch (kind) {
      case 0:
        return "EpubReader";
      case 1:
        return "TxtReader";
      default:
        return "XtcReader";
    }
  }

  static void verifyDictionaryElisions() {
    Dictionary::setLookupDictPathOverride("/dictionary-smoke/dict");
    static constexpr const char* cases[][2] = {
        {"l'histoire", "histoire"}, {"l’inspecteur", "inspecteur"},
        {"d'histoire", "histoire"}, {"qu'après", "après"},
        {"QU’Après", "après"},      {"Lʼécole", "école"},
        {"l’école", "école"},       {"j'aime", "aime"},
        {"n’aime", "aime"},         {"m'aime", "aime"},
        {"s’aime", "aime"},         {"t'aime", "aime"},
        {"c’est", "est"},
    };
    for (const auto& entry : cases) {
      bool matchedStem = false;
      const auto result = Dictionary::locateWithStemVariants(Dictionary::cleanWord(entry[0]), &matchedStem);
      if (!result.found || result.readError || result.headword != entry[1] || !matchedStem)
        fail("French dictionary lookup failed: %s -> %s", entry[0], entry[1]);
    }
    bool matchedStem = true;
    const auto exact = Dictionary::locateWithStemVariants("d'accord", &matchedStem);
    if (!exact.found || exact.headword != "d'accord" || matchedStem)
      fail("Exact dictionary headword must take priority over elision");
    for (const char* word : {"don't", "aujourd’hui", "l'", "qu’", "élèves"}) {
      const auto variants = Dictionary::getStemVariants(word);
      if (!variants.empty()) fail("Unexpected dictionary variant for %s", word);
    }
    const auto nameVariants = Dictionary::getStemVariants("O'Brien");
    if (std::find(nameVariants.begin(), nameVariants.end(), "brien") != nameVariants.end() ||
        std::find(nameVariants.begin(), nameVariants.end(), "Brien") != nameVariants.end())
      fail("Unrecognized apostrophe prefix was stripped");
    const auto stems = Dictionary::getStemVariants("running");
    if (std::find(stems.begin(), stems.end(), "run") == stems.end()) fail("English stemming regressed");
    const DictLookupCallbacks cancelled{nullptr, nullptr, [](void*) { return true; }};
    if (Dictionary::locateWithStemVariants("l'histoire", &matchedStem, cancelled).found)
      fail("Cancelled dictionary lookup returned a match");
    Dictionary::clearLookupDictPathOverride();
    LOG_INF("SMOKE", "Simulator smoke test passed: French dictionary elisions and exact-match priority");
    std::_Exit(0);
  }

  static bool enabled() { return std::getenv("CROSSINK_SIMULATOR_SMOKE_TEST") != nullptr; }

  static int pageTurnCount() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS");
    if (raw == nullptr || raw[0] == '\0') {
      return 2;
    }
    return std::max(0, std::atoi(raw));
  }

  static bool landscapeReaderRequested() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_LANDSCAPE_READER");
    return raw != nullptr && raw[0] != '\0' && raw[0] != '0';
  }

  static void applyRequestedTheme() {
    const char* raw = std::getenv("CROSSINK_SIMULATOR_SMOKE_THEME");
    if (raw == nullptr || raw[0] == '\0') {
      return;
    }

    const int theme = std::atoi(raw);
    if (theme < 0 || theme >= CrossPointSettings::UI_THEME_COUNT) {
      fail("Invalid smoke test theme index: %d", theme);
    }

    SETTINGS.uiTheme = static_cast<uint8_t>(theme);
    UITheme::getInstance().reload();
    LOG_INF("SMOKE", "Using theme index %d", theme);
  }

  static void verifyMixedPageGestures() {
#if CROSSINK_APP_CAP_TOUCH
    if (!gpio.hasTouch()) return;
    const uint8_t savedNext = SETTINGS.pageTurnGesture;
    const uint8_t savedPrevious = SETTINGS.previousPageGesture;
    const int width = renderer.getScreenWidth();
    const int y = renderer.getScreenHeight() / 2;
    mappedInputManager.setReaderMode(true);
    for (uint8_t next = 0; next < CrossPointSettings::PAGE_TURN_GESTURE_COUNT; ++next) {
      for (uint8_t previous = 0; previous < CrossPointSettings::PAGE_TURN_GESTURE_COUNT; ++previous) {
        SETTINGS.pageTurnGesture = next;
        SETTINGS.previousPageGesture = previous;
        const bool inverted = next == CrossPointSettings::INVERTED_TAP || previous == CrossPointSettings::INVERTED_TAP;
        const bool nextTap = next == CrossPointSettings::TAP_AND_SWIPE || next == CrossPointSettings::TAP_ONLY ||
                             next == CrossPointSettings::INVERTED_TAP;
        const bool previousTap = previous == CrossPointSettings::TAP_AND_SWIPE ||
                                 previous == CrossPointSettings::TAP_ONLY ||
                                 previous == CrossPointSettings::INVERTED_TAP;
        for (const int x : {0, width / 3 - 1, width / 3, width * 2 / 3 - 1, width * 2 / 3, width - 1}) {
          mappedInputManager.simulatorInjectTouchDown(x, y);
          mappedInputManager.simulatorInjectTouchRelease(x, y);
          const auto result = ReaderUtils::detectTouchPageTurn(renderer, mappedInputManager);
          const bool nextZone = inverted ? x < width * 2 / 3 : x >= width / 3;
          const bool expectedNext = nextTap && (!previousTap || nextZone);
          const bool expectedPrevious = previousTap && (!nextTap || !nextZone);
          if (!result.tapped || result.next != expectedNext || result.prev != expectedPrevious) {
            fail("Mixed page tap mismatch: next=%u previous=%u x=%d", next, previous, x);
          }
          mappedInputManager.simulatorClearInputFrame();
        }
        for (const bool right : {false, true}) {
          const int startX = right ? 1 : width - 2;
          const int endX = right ? width - 2 : 1;
          mappedInputManager.simulatorInjectTouchDown(startX, y);
          mappedInputManager.simulatorInjectTouchMove(endX, y);
          mappedInputManager.simulatorInjectTouchRelease(endX, y);
          const auto result = ReaderUtils::detectTouchPageTurn(renderer, mappedInputManager);
          const uint8_t mode = right ? previous : next;
          const bool expected = (mode == CrossPointSettings::TAP_AND_SWIPE || mode == CrossPointSettings::SWIPE_ONLY);
          if (result.next != (!right && expected) || result.prev != (right && expected) ||
              (right && !expected && mappedInputManager.wasReleased(MappedInputManager::Button::Back))) {
            fail("Mixed page swipe mismatch: next=%u previous=%u right=%d", next, previous, right);
          }
          mappedInputManager.simulatorClearInputFrame();
        }
      }
    }
    SETTINGS.pageTurnGesture = savedNext;
    SETTINGS.previousPageGesture = savedPrevious;
    mappedInputManager.setReaderMode(false);
    LOG_INF("SMOKE", "All 25 mixed page gesture combinations passed");
#endif
  }

  static void verifyStatusBarSettings() {
    JsonDocument original;
    SETTINGS.toJson(original);
    const HalClock originalClock = halClock;
    halClock = HalClock{};  // Simulate a failed RTC probe during settings loading.
    for (const int clock : {0, 1}) {
      JsonDocument legacy;
      legacy.set(original);
      legacy.remove("displayStatusBar");
      legacy["showClockOutsideReader"] = clock;
      SETTINGS.fromJson(legacy.as<JsonVariantConst>());
      if (SETTINGS.displayStatusBar.slots[1] != (clock ? ReaderStatusBarItem::Clock : ReaderStatusBarItem::Empty) ||
          SETTINGS.displayStatusBar.slots[2] != ReaderStatusBarItem::Battery)
        fail("Display clock migration failed");
    }
    // Hide Battery % (Never / In Reader / Always) migrates into per-bar battery styles once.
    using Style = ReaderStatusBarBatteryStyle;
    constexpr Style expectedReader[] = {Style::IconAndPercent, Style::IconOnly, Style::IconOnly};
    constexpr Style expectedDisplay[] = {Style::IconAndPercent, Style::IconAndPercent, Style::IconOnly};
    for (const uint8_t hide : {0, 1, 2}) {
      JsonDocument legacy;
      legacy.set(original);
      legacy.remove("displayBatteryStyle");
      legacy["readerStatusBars"]["top"].remove("battery");
      legacy["readerStatusBars"]["bottom"].remove("battery");
      legacy["hideBatteryPercentage"] = hide;
      SETTINGS.fromJson(legacy.as<JsonVariantConst>());
      if (SETTINGS.topReaderStatusBar.batteryStyle != expectedReader[hide] ||
          SETTINGS.bottomReaderStatusBar.batteryStyle != expectedReader[hide] ||
          SETTINGS.displayStatusBar.batteryStyle != expectedDisplay[hide])
        fail("Battery style migration failed for hideBatteryPercentage=%u", hide);
      JsonDocument migrated;
      SETTINGS.toJson(migrated);
      migrated["hideBatteryPercentage"] = 0;  // A stale legacy key must not override saved styles.
      SETTINGS.fromJson(migrated.as<JsonVariantConst>());
      if (SETTINGS.bottomReaderStatusBar.batteryStyle != expectedReader[hide] ||
          SETTINGS.displayStatusBar.batteryStyle != expectedDisplay[hide])
        fail("Battery style did not survive a save round trip for hideBatteryPercentage=%u", hide);
    }
    SETTINGS.fromJson(original.as<JsonVariantConst>());
    SETTINGS.displayStatusBar.slots = {ReaderStatusBarItem::Date, ReaderStatusBarItem::Clock,
                                       ReaderStatusBarItem::Empty};
    SETTINGS.topReaderStatusBar.slots[ReaderStatusBarConfig::CENTER] = ReaderStatusBarItem::Clock;
    SETTINGS.bottomReaderStatusBar.slots[ReaderStatusBarConfig::CENTER] = ReaderStatusBarItem::Date;
    JsonDocument saved;
    SETTINGS.toJson(saved);
    if (!saved["showClockOutsideReader"].isNull()) fail("Obsolete clock setting was saved");
    SETTINGS.displayStatusBar = DisplayStatusBarConfig{};
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    if (SETTINGS.displayStatusBar.slots[0] != ReaderStatusBarItem::Date ||
        SETTINGS.displayStatusBar.slots[1] != ReaderStatusBarItem::Clock ||
        SETTINGS.displayStatusBar.slots[2] != ReaderStatusBarItem::Empty)
      fail("Display slots did not survive reload");
    if (SETTINGS.topReaderStatusBar.slots[ReaderStatusBarConfig::CENTER] != ReaderStatusBarItem::Clock ||
        SETTINGS.bottomReaderStatusBar.slots[ReaderStatusBarConfig::CENTER] != ReaderStatusBarItem::Date)
      fail("Reader clock/date slots did not survive an unavailable RTC");
    halClock = originalClock;
    const auto display = buildGroupedDisplaySettingsList(getSettingsList());
    if (std::none_of(display.begin(), display.end(),
                     [](const auto& item) { return item.action == SettingAction::DisplayStatusBar; }))
      fail("Display status bar setting is missing");
    SETTINGS.fromJson(original.as<JsonVariantConst>());
    LOG_INF("SMOKE", "Display status bar migration and persistence passed");
  }

  static void captureStatusBarScreen(const char* name) {
    const char* output = std::getenv("CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_CAPTURES");
    if (!output) return;
    std::filesystem::create_directories(output);
    const auto path = std::filesystem::path(output) / (std::string(name) + ".pgm");
    FILE* image = std::fopen(path.c_str(), "wb");
    if (!image) fail("Cannot create status bar capture");
    const int width = renderer.getScreenWidth();
    const int height = renderer.getScreenHeight();
    std::fprintf(image, "P5\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
    }
    std::fclose(image);
  }

  static void verifyReaderControlsSettings() {
    const auto& base = getBaseSettingsList();
    const size_t expectedBaseSettingsCapacity = getBaseSettingsCapacity();
    if (base.size() != expectedBaseSettingsCapacity || base.capacity() < expectedBaseSettingsCapacity) {
      fail("Base settings allocation mismatch: size=%zu expected=%zu capacity=%zu", base.size(),
           expectedBaseSettingsCapacity, base.capacity());
    }
    const auto all = getSettingsList();
    const auto controls = buildControlsSettingsParentList(all);
    if (!deviceHasFrontButtons()) {
      if (hasSettingByName(all, StrId::STR_MENU_NAVIGATION) || hasSettingByName(controls, StrId::STR_MENU_NAVIGATION))
        fail("Menu navigation must be hidden on devices without front buttons");
    } else if (controls.empty() || controls.back().valuePtr != &CrossPointSettings::menuNavigation) {
      fail("Menu navigation must be the last shared Controls setting on front-button devices");
    }
    JsonDocument navigationOriginal;
    SETTINGS.toJson(navigationOriginal);
    SETTINGS.menuNavigation = CrossPointSettings::MENU_NAV_DIRECTIONAL;
    JsonDocument legacyNavigation;
    SETTINGS.fromJson(legacyNavigation.as<JsonVariantConst>());
    if (SETTINGS.menuNavigation != CrossPointSettings::MENU_NAV_DIRECTIONAL)
      fail("Missing menu navigation must preserve the Directional default");
    for (const uint8_t mode : {CrossPointSettings::MENU_NAV_DIRECTIONAL, CrossPointSettings::MENU_NAV_CLASSIC}) {
      SETTINGS.menuNavigation = mode;
      JsonDocument saved;
      SETTINGS.toJson(saved);
      if (saved["menuNavigation"].as<uint8_t>() != mode) fail("Menu navigation missing from settings JSON");
      SETTINGS.menuNavigation = 255;
      SETTINGS.fromJson(saved.as<JsonVariantConst>());
      if (SETTINGS.menuNavigation != mode) fail("Menu navigation JSON round trip failed");
    }
    SETTINGS.menuNavigation = CrossPointSettings::MENU_NAV_DIRECTIONAL;
    legacyNavigation["menuNavigation"] = 255;
    SETTINGS.fromJson(legacyNavigation.as<JsonVariantConst>());
    if (SETTINGS.menuNavigation != CrossPointSettings::MENU_NAV_DIRECTIONAL)
      fail("Invalid menu navigation must preserve the default");
    SETTINGS.fromJson(navigationOriginal.as<JsonVariantConst>());
    const auto gestures = buildControlsTapsGesturesSettingsList(all);
    if (gpio.hasTouch()) {
      if (gestures.size() < 3 || gestures[0].nameId != StrId::STR_NEXT_PAGE ||
          gestures[1].nameId != StrId::STR_PREV_PAGE || gestures[0].enumValues != gestures[1].enumValues) {
        fail("Page gesture settings order/options mismatch");
      }
      if (gpio.supportsMultiTouch() && (gestures.size() < 4 || gestures[3].nameId != StrId::STR_TWO_FINGER_ROTATION)) {
        fail("Two-finger rotation gesture setting order mismatch");
      }
      const size_t statusIndex = gpio.supportsMultiTouch() ? 4 : 2;
      if (gestures.size() <= statusIndex || gestures[statusIndex].nameId != StrId::STR_TAP_HIDE_STATUS_BAR) {
        fail("Status bar gesture setting order mismatch");
      }
    } else if (!gestures.empty()) {
      fail("Touch gestures exposed on a button-only device");
    }
    const auto device = buildSystemDeviceSettingsList(all);
    if (device.size() < 3 || device[1].nameId != StrId::STR_TIME_TO_SLEEP ||
        device[2].nameId != StrId::STR_CUSTOM_BOOTSCREEN) {
      fail("Custom bootscreen setting order mismatch");
    }
    JsonDocument original;
    SETTINGS.toJson(original);
    for (uint8_t mode = 0; mode <= CrossPointSettings::PAGE_TURN_GESTURE_DISABLED; ++mode) {
      JsonDocument legacy;
      legacy["pageTurnGesture"] = mode;
      SETTINGS.fromJson(legacy.as<JsonVariantConst>());
      if (SETTINGS.pageTurnGesture != mode || SETTINGS.previousPageGesture != mode) {
        fail("Legacy page gesture migration mismatch");
      }
    }
    constexpr uint8_t importedGestures[] = {CrossPointSettings::TAP_AND_SWIPE, CrossPointSettings::TAP_ONLY,
                                            CrossPointSettings::SWIPE_ONLY, CrossPointSettings::INVERTED_TAP};
    for (uint8_t mode = 0; mode < 4; ++mode) {
      JsonDocument crosspoint;
      crosspoint["touchReaderControls"] = mode;
      crosspoint["disableReaderTouchscreen"] = 1;
      SETTINGS.fromJson(crosspoint.as<JsonVariantConst>(), true);
      if (SETTINGS.disableReaderTouchscreen || SETTINGS.touchReaderControls != (mode != 0) ||
          SETTINGS.pageTurnGesture != importedGestures[mode] ||
          SETTINGS.previousPageGesture != importedGestures[mode]) {
        fail("CrossPoint touch settings migration mismatch");
      }
      JsonDocument migrated;
      SETTINGS.toJson(migrated);
      SETTINGS.disableReaderTouchscreen = 1;
      SETTINGS.pageTurnGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
      SETTINGS.previousPageGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
      SETTINGS.fromJson(migrated.as<JsonVariantConst>());
      if (SETTINGS.disableReaderTouchscreen || SETTINGS.touchReaderControls != (mode != 0) ||
          SETTINGS.pageTurnGesture != importedGestures[mode] ||
          SETTINGS.previousPageGesture != importedGestures[mode]) {
        fail("Migrated CrossPoint touch settings did not survive reload");
      }
    }
    // The namespaced file must preserve intentional locks, including older files without gesture keys.
    JsonDocument locked;
    locked["touchReaderControls"] = 1;
    locked["disableReaderTouchscreen"] = 1;
    SETTINGS.fromJson(locked.as<JsonVariantConst>());
    if (!SETTINGS.disableReaderTouchscreen) fail("CrossInk touch lock was lost");
    locked["pageTurnGesture"] = CrossPointSettings::TAP_ONLY;
    locked["previousPageGesture"] = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    SETTINGS.fromJson(locked.as<JsonVariantConst>(), true);
    if (!SETTINGS.disableReaderTouchscreen || SETTINGS.pageTurnGesture != CrossPointSettings::TAP_ONLY ||
        SETTINGS.previousPageGesture != CrossPointSettings::PAGE_TURN_GESTURE_DISABLED) {
      fail("Legacy CrossInk gesture settings were treated as CrossPoint");
    }
    SETTINGS.previousPageGesture = CrossPointSettings::SWIPE_ONLY;
    SETTINGS.pageTurnGesture = CrossPointSettings::TAP_ONLY;
    SETTINGS.customBootscreenEnabled = 0;
    SETTINGS.tapToHideStatusBar = 0;
    JsonDocument saved;
    SETTINGS.toJson(saved);
    SETTINGS.previousPageGesture = CrossPointSettings::TAP_AND_SWIPE;
    SETTINGS.customBootscreenEnabled = 1;
    SETTINGS.tapToHideStatusBar = 1;
    SETTINGS.fromJson(saved.as<JsonVariantConst>());
    if (SETTINGS.previousPageGesture != CrossPointSettings::SWIPE_ONLY ||
        SETTINGS.pageTurnGesture != CrossPointSettings::TAP_ONLY || SETTINGS.customBootscreenEnabled ||
        SETTINGS.tapToHideStatusBar) {
      fail("Reader controls settings round-trip mismatch");
    }
    constexpr char CROSSINK_SETTINGS_FILE_BAK[] = "/.crosspoint/crossink-settings.json.bak";
    constexpr char LEGACY_SETTINGS_FILE_JSON[] = "/.crosspoint/settings.json";
    const char* const crossInkSettingsPath = CrossPointSettings::getFilePath();
    const bool hadCrossInkSettings = Storage.exists(crossInkSettingsPath);
    const String savedCrossInkSettings = hadCrossInkSettings ? Storage.readFile(crossInkSettingsPath) : String();
    const bool hadCrossInkSettingsBackup = Storage.exists(CROSSINK_SETTINGS_FILE_BAK);
    const String savedCrossInkSettingsBackup =
        hadCrossInkSettingsBackup ? Storage.readFile(CROSSINK_SETTINGS_FILE_BAK) : String();
    const bool hadLegacySettings = Storage.exists(LEGACY_SETTINGS_FILE_JSON);
    const String savedLegacySettings = hadLegacySettings ? Storage.readFile(LEGACY_SETTINGS_FILE_JSON) : String();

    JsonDocument crossInkSettings;
    crossInkSettings["touchReaderControls"] = CrossPointSettings::TOUCH_READER_ON;
    crossInkSettings["pageTurnGesture"] = CrossPointSettings::TAP_ONLY;
    crossInkSettings["previousPageGesture"] = CrossPointSettings::SWIPE_ONLY;
    crossInkSettings["disableReaderTouchscreen"] = 0;
    String crossInkJson;
    serializeJson(crossInkSettings, crossInkJson);

    JsonDocument crossPointSettings;
    crossPointSettings["touchReaderControls"] = 2;
    crossPointSettings["disableReaderTouchscreen"] = 1;
    String crossPointJson;
    serializeJson(crossPointSettings, crossPointJson);

    if (!Storage.writeFile(crossInkSettingsPath, crossInkJson) ||
        !Storage.writeFile(LEGACY_SETTINGS_FILE_JSON, crossPointJson)) {
      fail("Could not write settings migration test fixture");
    }
    SETTINGS.disableReaderTouchscreen = 1;
    SETTINGS.pageTurnGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    SETTINGS.previousPageGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    if (!SETTINGS.loadFromFile() || SETTINGS.disableReaderTouchscreen ||
        SETTINGS.pageTurnGesture != CrossPointSettings::TAP_ONLY ||
        SETTINGS.previousPageGesture != CrossPointSettings::SWIPE_ONLY) {
      fail("CrossInk settings file did not take precedence over CrossPoint settings");
    }

    // A corrupt CrossInk file still blocks the foreign fallback. It is safer
    // to leave settings unchanged than to silently import CrossPoint values.
    if (!Storage.writeFile(crossInkSettingsPath, "{")) fail("Could not corrupt CrossInk settings test fixture");
    if (SETTINGS.loadFromFile() || SETTINGS.disableReaderTouchscreen ||
        SETTINGS.pageTurnGesture != CrossPointSettings::TAP_ONLY ||
        SETTINGS.previousPageGesture != CrossPointSettings::SWIPE_ONLY) {
      fail("Corrupt CrossInk settings fell through to CrossPoint settings");
    }

    // An interrupted atomic replacement leaves the CrossInk backup as the
    // sole namespaced file. Recover it before considering CrossPoint's file.
    if (!Storage.writeFile(CROSSINK_SETTINGS_FILE_BAK, crossInkJson) || !Storage.remove(crossInkSettingsPath)) {
      fail("Could not create interrupted CrossInk settings fixture");
    }
    SETTINGS.disableReaderTouchscreen = 1;
    SETTINGS.pageTurnGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    SETTINGS.previousPageGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    if (!SETTINGS.loadFromFile() || SETTINGS.disableReaderTouchscreen ||
        SETTINGS.pageTurnGesture != CrossPointSettings::TAP_ONLY ||
        SETTINGS.previousPageGesture != CrossPointSettings::SWIPE_ONLY || !Storage.exists(crossInkSettingsPath) ||
        Storage.exists(CROSSINK_SETTINGS_FILE_BAK)) {
      fail("Interrupted CrossInk settings save did not recover before CrossPoint import");
    }

    if (hadCrossInkSettings) {
      if (!Storage.writeFile(crossInkSettingsPath, savedCrossInkSettings)) fail("Could not restore CrossInk settings");
    } else if (Storage.exists(crossInkSettingsPath) && !Storage.remove(crossInkSettingsPath)) {
      fail("Could not remove CrossInk settings test fixture");
    }
    if (hadCrossInkSettingsBackup) {
      if (!Storage.writeFile(CROSSINK_SETTINGS_FILE_BAK, savedCrossInkSettingsBackup)) {
        fail("Could not restore CrossInk settings backup");
      }
    } else if (Storage.exists(CROSSINK_SETTINGS_FILE_BAK) && !Storage.remove(CROSSINK_SETTINGS_FILE_BAK)) {
      fail("Could not remove CrossInk settings backup fixture");
    }
    if (hadLegacySettings) {
      if (!Storage.writeFile(LEGACY_SETTINGS_FILE_JSON, savedLegacySettings)) fail("Could not restore legacy settings");
    } else if (Storage.exists(LEGACY_SETTINGS_FILE_JSON) && !Storage.remove(LEGACY_SETTINGS_FILE_JSON)) {
      fail("Could not remove legacy settings test fixture");
    }

    SETTINGS.librarySortMethod = 3;
    SETTINGS.librarySortDescending = 0;
    SETTINGS.libraryListExpanded = 1;
    SETTINGS.recentBooksView = CrossPointSettings::RECENT_BOOKS_GRID;
    SETTINGS.libraryShowMarkdown = 0;
    SETTINGS.libraryHideFinishedBooks = 1;
    JsonDocument librarySaved;
    SETTINGS.toJson(librarySaved);
    SETTINGS.librarySortMethod = 0;
    SETTINGS.librarySortDescending = 1;
    SETTINGS.libraryListExpanded = 0;
    SETTINGS.recentBooksView = CrossPointSettings::RECENT_BOOKS_LIST;
    SETTINGS.libraryShowMarkdown = 1;
    SETTINGS.libraryHideFinishedBooks = 0;
    SETTINGS.fromJson(librarySaved.as<JsonVariantConst>());
    if (SETTINGS.librarySortMethod != 3 || SETTINGS.librarySortDescending || !SETTINGS.libraryListExpanded ||
        SETTINGS.recentBooksView != CrossPointSettings::RECENT_BOOKS_GRID || SETTINGS.libraryShowMarkdown ||
        !SETTINGS.libraryHideFinishedBooks) {
      fail("Library settings round-trip mismatch");
    }
    librarySaved["librarySortMethod"] = 99;
    librarySaved["libraryShowTxt"] = 2;
    librarySaved["libraryHideFinishedBooks"] = 2;
    librarySaved["recentBooksView"] = 2;
    SETTINGS.fromJson(librarySaved.as<JsonVariantConst>());
    if (SETTINGS.librarySortMethod != 3 || SETTINGS.libraryShowTxt != 1 || !SETTINGS.libraryHideFinishedBooks ||
        SETTINGS.recentBooksView != CrossPointSettings::RECENT_BOOKS_LIST) {
      fail("Invalid Library settings were not rejected");
    }
    librarySaved.remove("recentBooksView");
    SETTINGS.recentBooksView = CrossPointSettings::RECENT_BOOKS_GRID;
    SETTINGS.fromJson(librarySaved.as<JsonVariantConst>());
    if (SETTINGS.recentBooksView != CrossPointSettings::RECENT_BOOKS_LIST)
      fail("Missing Recently Opened view did not default to List");
    SETTINGS.fromJson(original.as<JsonVariantConst>());
  }

  static void verifyUpDownShortcutAvailability() {
    const auto allSettings = getSettingsList();
    const auto sideButtonSettings = buildControlsSideButtonSettingsList(allSettings);
    const bool hasSideButtonChord =
        std::any_of(sideButtonSettings.begin(), sideButtonSettings.end(),
                    [](const SettingInfo& setting) { return setting.nameId == StrId::STR_SIDE_BUTTON_CHORD; });
    if (hasSideButtonChord != deviceSupportsSideButtonChord(gpio)) {
      fail("Side-button chord availability does not match device controls");
    }

    if (QuickActionsActivityTest::isTriggerAvailable(QuickActions::Trigger::UpDown) !=
        deviceSupportsSideButtonChord(gpio)) {
      fail("Quick Actions Up + Down availability does not match device controls");
    }

    const auto chordSetting = std::find_if(allSettings.begin(), allSettings.end(), [](const SettingInfo& setting) {
      return settingKeyIs(setting, "powerChordAction");
    });
    if (chordSetting == allSettings.end()) fail("Power chord setting is missing");
    if (std::find(chordSetting->enumRawValues.begin(), chordSetting->enumRawValues.end(),
                  CrossPointSettings::CHORD_QUICK_ACTIONS) == chordSetting->enumRawValues.end()) {
      fail("Quick Actions is missing from the Power + Up chord setting");
    }
    if (CrossPointSettings::HOME_READER != 36 || CrossPointSettings::SELECT_CHAPTER != 38 ||
        CrossPointSettings::SHORT_PWRBTN_COUNT != 39 || CrossPointSettings::CHORD_HOME_READER != 32 ||
        CrossPointSettings::CHORD_SELECT_CHAPTER != 34 || CrossPointSettings::POWER_CHORD_ACTION_COUNT != 35) {
      fail("Home/Reader changed persisted shortcut IDs or counts");
    }
    if (QuickActions::actionLabel(CrossPointSettings::HOME_READER) != StrId::STR_HOME_READER) {
      fail("Home/Reader shortcut label mismatch");
    }
    if (!QuickActions::isActionAvailable(CrossPointSettings::HOME_READER)) {
      fail("Home/Reader is unavailable on this device");
    }
    const auto containsShortcut = [](const std::vector<SettingInfo>& settings, const char* key,
                                     const ShortcutOptionCatalog catalog,
                                     const CrossPointSettings::SHORT_PWRBTN action = CrossPointSettings::HOME_READER) {
      const auto setting = std::find_if(settings.begin(), settings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (setting == settings.end()) return false;
      const uint8_t raw = shortcutRawValue(catalog, action);
      const auto choice = std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), raw);
      return choice != setting->enumRawValues.end() &&
             setting->enumValues[static_cast<size_t>(choice - setting->enumRawValues.begin())] ==
                 QuickActions::actionLabel(action) &&
             std::count(setting->enumRawValues.begin(), setting->enumRawValues.end(), raw) == 1;
    };
    if (!containsShortcut(allSettings, "shortPwrBtn", ShortcutOptionCatalog::PowerButton) ||
        !containsShortcut(sideButtonSettings, "sideButtonUpShort", ShortcutOptionCatalog::SideButton) ||
        !containsShortcut(allSettings, "powerChordAction", ShortcutOptionCatalog::ButtonChord)) {
      fail("Home/Reader shortcut availability or settings mapping mismatch");
    }
    for (const auto action : {CrossPointSettings::BACK_HOME, CrossPointSettings::HOME_READER}) {
      if (!QuickActions::isQuickActionSlotActionAvailable(action)) fail("Navigation missing from Quick Actions");
      for (const char* key : {"shortPwrBtn", "longPwrBtn"}) {
        if (!containsShortcut(allSettings, key, ShortcutOptionCatalog::PowerButton, action))
          fail("Navigation missing from power shortcut %s", key);
      }
      for (const char* key : {"longPressMenuAction", "longPressBackAction"}) {
        if (!containsShortcut(allSettings, key, ShortcutOptionCatalog::LongPress, action))
          fail("Navigation missing from long-press shortcut %s", key);
      }
      if (!containsShortcut(allSettings, "powerChordAction", ShortcutOptionCatalog::ButtonChord, action))
        fail("Navigation missing from power chord");
      if (deviceSupportsSideButtonChord(gpio) &&
          !containsShortcut(allSettings, "sideButtonChordAction", ShortcutOptionCatalog::ButtonChord, action))
        fail("Navigation missing from side chord");
      for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
        if (!containsShortcut(sideButtonSettings, key, ShortcutOptionCatalog::SideButton, action))
          fail("Navigation missing from side shortcut %s", key);
      }
      if (gpio.hasHomeKey()) {
        for (const char* key : {"homeButtonTapAction", "homeButtonDoubleTapAction", "homeButtonLongPressAction"}) {
          if (!containsShortcut(allSettings, key, ShortcutOptionCatalog::HomeButton, action))
            fail("Navigation missing or duplicated in Home shortcut %s", key);
        }
      }
    }
    if (CrossPointSettings::BACK_HOME != 37 || CrossPointSettings::CHORD_BACK_HOME != 33 ||
        CrossPointSettings::HOME_BUTTON_BACK_HOME != 23 || CrossPointSettings::LONG_MENU_HOME_READER != 25 ||
        CrossPointSettings::LONG_MENU_BACK_HOME != 26)
      fail("Navigation changed persisted IDs");
    JsonDocument savedNavigationSettings;
    SETTINGS.toJson(savedNavigationSettings);
    for (const auto action : {CrossPointSettings::BACK_HOME, CrossPointSettings::HOME_READER}) {
      const auto chord = shortcutRawValue(ShortcutOptionCatalog::ButtonChord, action);
      const auto longPress = shortcutRawValue(ShortcutOptionCatalog::LongPress, action);
      SETTINGS.shortPwrBtn = action;
      SETTINGS.longPwrBtn = action;
      SETTINGS.powerChordAction = chord;
      SETTINGS.sideButtonUpLong = action;
      SETTINGS.longPressMenuAction = longPress;
      SETTINGS.longPressBackAction = longPress;
      SETTINGS.quickActionSlots[0] = action;
      JsonDocument roundTrip;
      SETTINGS.toJson(roundTrip);
      SETTINGS.fromJson(savedNavigationSettings.as<JsonVariantConst>());
      SETTINGS.fromJson(roundTrip.as<JsonVariantConst>());
      if (SETTINGS.shortPwrBtn != action || SETTINGS.longPwrBtn != action || SETTINGS.powerChordAction != chord ||
          SETTINGS.sideButtonUpLong != action || SETTINGS.longPressMenuAction != longPress ||
          SETTINGS.longPressBackAction != longPress || SETTINGS.quickActionSlots[0] != action)
        fail("Navigation assignments did not survive settings reload");
    }
    SETTINGS.fromJson(savedNavigationSettings.as<JsonVariantConst>());
    for (const auto action :
         {CrossPointSettings::TWO_FINGER_SWIPE_BACK_HOME, CrossPointSettings::TWO_FINGER_SWIPE_HOME_READER}) {
      if (!CrossPointSettings::isTwoFingerSwipeActionAvailable(action, false, false))
        fail("Navigation swipe unexpectedly requires a frontlight");
      if (gpio.hasTouch()) {
        SETTINGS.leftEdgeUp = action;
        if (gpio.supportsMultiTouch()) SETTINGS.twoFingerSwipeUp = action;
        JsonDocument swipeRoundTrip;
        SETTINGS.toJson(swipeRoundTrip);
        SETTINGS.fromJson(savedNavigationSettings.as<JsonVariantConst>());
        SETTINGS.fromJson(swipeRoundTrip.as<JsonVariantConst>());
        if (SETTINGS.leftEdgeUp != action || (gpio.supportsMultiTouch() && SETTINGS.twoFingerSwipeUp != action))
          fail("Navigation swipe did not survive settings reload");
      }
    }
    SETTINGS.fromJson(savedNavigationSettings.as<JsonVariantConst>());
    if (gpio.hasHomeKey()) {
      const auto homeSettings = buildControlsHomeButtonSettingsList(allSettings);
      for (const char* key : {"homeButtonTapAction", "homeButtonDoubleTapAction", "homeButtonLongPressAction"}) {
        if (!containsShortcut(homeSettings, key, ShortcutOptionCatalog::HomeButton)) {
          fail("Home/Reader is missing from a Home-button shortcut picker");
        }
      }
      for (auto field : {&CrossPointSettings::homeButtonTapAction, &CrossPointSettings::homeButtonDoubleTapAction,
                         &CrossPointSettings::homeButtonLongPressAction}) {
        const uint8_t saved = SETTINGS.*field;
        SETTINGS.*field = CrossPointSettings::HOME_READER;
        JsonDocument homeRoundTrip;
        SETTINGS.toJson(homeRoundTrip);
        SETTINGS.*field = CrossPointSettings::IGNORE;
        SETTINGS.fromJson(homeRoundTrip.as<JsonVariantConst>());
        if (SETTINGS.*field != CrossPointSettings::HOME_READER) {
          fail("Home/Reader Home-button assignment did not survive settings reload");
        }
        SETTINGS.*field = saved;
      }
    }
    const uint8_t savedPowerAction = SETTINGS.shortPwrBtn;
    const uint8_t savedChordAction = SETTINGS.powerChordAction;
    const uint8_t savedQuickActionSlot = SETTINGS.quickActionSlots[0];
    SETTINGS.shortPwrBtn = CrossPointSettings::HOME_READER;
    SETTINGS.powerChordAction = CrossPointSettings::CHORD_HOME_READER;
    SETTINGS.quickActionSlots[0] = CrossPointSettings::HOME_READER;
    JsonDocument shortcutRoundTrip;
    SETTINGS.toJson(shortcutRoundTrip);
    SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
    SETTINGS.powerChordAction = CrossPointSettings::CHORD_DISABLED;
    SETTINGS.quickActionSlots[0] = CrossPointSettings::IGNORE;
    SETTINGS.fromJson(shortcutRoundTrip.as<JsonVariantConst>());
    if (SETTINGS.shortPwrBtn != CrossPointSettings::HOME_READER ||
        SETTINGS.powerChordAction != CrossPointSettings::CHORD_HOME_READER ||
        SETTINGS.quickActionSlots[0] != CrossPointSettings::HOME_READER) {
      fail("Home/Reader settings round-trip did not match device availability");
    }
    SETTINGS.shortPwrBtn = savedPowerAction;
    SETTINGS.powerChordAction = savedChordAction;
    SETTINGS.quickActionSlots[0] = savedQuickActionSlot;
    if (!gpio.hasHomeKey() &&
        std::find(chordSetting->enumRawValues.begin(), chordSetting->enumRawValues.end(),
                  CrossPointSettings::CHORD_TOGGLE_HOME_BUTTON) != chordSetting->enumRawValues.end()) {
      fail("Toggle Home Button is still offered without a Home key");
    }
    if (std::find(chordSetting->enumRawValues.begin(), chordSetting->enumRawValues.end(),
                  CrossPointSettings::CHORD_PREVIOUS_PAGE) == chordSetting->enumRawValues.end()) {
      fail("Previous Page was removed by an unrelated power-button action ID");
    }
    if (!gpio.hasTouch() &&
        std::find(chordSetting->enumRawValues.begin(), chordSetting->enumRawValues.end(),
                  CrossPointSettings::CHORD_TOGGLE_TOUCHSCREEN) != chordSetting->enumRawValues.end()) {
      fail("Toggle Touchscreen is still offered without touch hardware");
    }
    if (!Frontlight.present() &&
        std::find(chordSetting->enumRawValues.begin(), chordSetting->enumRawValues.end(),
                  CrossPointSettings::CHORD_TOGGLE_FRONTLIGHT) != chordSetting->enumRawValues.end()) {
      fail("Toggle Frontlight is still offered without a frontlight");
    }

    const auto hasLibrary = [](const SettingInfo& setting, const ShortcutOptionCatalog catalog) {
      const auto raw = shortcutRawValue(catalog, CrossPointSettings::LIBRARY);
      const auto choice = std::find(setting.enumRawValues.begin(), setting.enumRawValues.end(), raw);
      return choice != setting.enumRawValues.end() &&
             setting.enumValues[static_cast<size_t>(choice - setting.enumRawValues.begin())] == StrId::STR_LIBRARY;
    };
    const auto verifyLibraryChoice = [&](const char* key, const ShortcutOptionCatalog catalog) {
      const auto setting = std::find_if(allSettings.begin(), allSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (setting == allSettings.end() || !hasLibrary(*setting, catalog)) {
        fail("Library shortcut is missing or mislabeled in %s", key);
      }
    };
    verifyLibraryChoice("shortPwrBtn", ShortcutOptionCatalog::PowerButton);
    verifyLibraryChoice("longPwrBtn", ShortcutOptionCatalog::PowerButton);
    for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
      verifyLibraryChoice(key, ShortcutOptionCatalog::SideButton);
      const auto setting = std::find_if(allSettings.begin(), allSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      for (const auto [action, suffix] :
           {std::pair{CrossPointSettings::SIDE_ROTATE_COUNTERCLOCKWISE, StrId::STR_ROTATE_CCW},
            std::pair{CrossPointSettings::SIDE_ROTATE_CLOCKWISE, StrId::STR_ROTATE_CW},
            std::pair{CrossPointSettings::SIDE_ROTATE_FLIP, StrId::STR_ROTATE_FLIP}}) {
        const auto choice = std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), action);
        const std::string expected = I18N.get(suffix);
        if (choice == setting->enumRawValues.end() ||
            sideButtonOptionLabel(*setting, static_cast<uint8_t>(choice - setting->enumRawValues.begin())) !=
                expected) {
          fail("Orientation shortcut is missing or mislabeled in %s", key);
        }
      }
    }
    for (uint8_t orientation = 0; orientation < CrossPointSettings::ORIENTATION_COUNT; ++orientation) {
      if (ReaderUtils::flippedOrientation(ReaderUtils::flippedOrientation(orientation)) != orientation ||
          ReaderUtils::flippedOrientation(orientation) !=
              ReaderUtils::rotatedOrientation(ReaderUtils::rotatedOrientation(orientation, true), true)) {
        fail("Flip must turn the screen 180 degrees from every orientation");
      }
    }
    verifyLibraryChoice("powerChordAction", ShortcutOptionCatalog::ButtonChord);
    verifyLibraryChoice("longPressMenuAction", ShortcutOptionCatalog::LongPress);
    verifyLibraryChoice("longPressBackAction", ShortcutOptionCatalog::LongPress);
    const auto verifySleepChoice = [&](const char* key, const ShortcutOptionCatalog catalog) {
      const auto setting = std::find_if(allSettings.begin(), allSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      const auto raw = shortcutRawValue(catalog, CrossPointSettings::SLEEP);
      if (setting == allSettings.end() || raw == SHORTCUT_OPTION_UNAVAILABLE) {
        fail("Sleep shortcut is missing from %s", key);
      }
      const auto choice = std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), raw);
      const std::string expected = catalog == ShortcutOptionCatalog::PowerButton
                                       ? std::string(tr(STR_SLEEP)) + "/" + tr(STR_WAKE)
                                       : tr(STR_SLEEP);
      if (choice == setting->enumRawValues.end() ||
          sideButtonOptionLabel(*setting, static_cast<uint8_t>(choice - setting->enumRawValues.begin())) != expected) {
        fail("Sleep shortcut has the wrong label in %s", key);
      }
    };
    verifySleepChoice("shortPwrBtn", ShortcutOptionCatalog::PowerButton);
    verifySleepChoice("longPwrBtn", ShortcutOptionCatalog::PowerButton);
    for (const char* key : {"shortPwrBtn", "longPwrBtn"}) {
      const auto setting = std::find_if(allSettings.begin(), allSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (setting == allSettings.end()) fail("Power shortcut setting is missing: %s", key);
      for (const auto [action, label] : {std::pair{CrossPointSettings::SLEEP_ONLY, StrId::STR_SLEEP},
                                         std::pair{CrossPointSettings::WAKE_ONLY, StrId::STR_WAKE}}) {
        const auto choice = std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), action);
        if (choice == setting->enumRawValues.end() ||
            sideButtonOptionLabel(*setting, static_cast<uint8_t>(choice - setting->enumRawValues.begin())) !=
                I18N.get(label)) {
          fail("Power-only shortcut is missing or mislabeled in %s", key);
        }
      }
    }
    const uint8_t savedShortPowerAction = SETTINGS.shortPwrBtn;
    for (const auto [action, wakes] :
         {std::pair{CrossPointSettings::IGNORE, false}, std::pair{CrossPointSettings::SLEEP_ONLY, false},
          std::pair{CrossPointSettings::WAKE_ONLY, true}, std::pair{CrossPointSettings::SLEEP, true}}) {
      SETTINGS.shortPwrBtn = action;
      if (SETTINGS.shortPowerPressWakes() != wakes) fail("Short Power wake policy does not match its shortcut");
    }
    SETTINGS.shortPwrBtn = savedShortPowerAction;
    verifySleepChoice("powerChordAction", ShortcutOptionCatalog::ButtonChord);
    verifySleepChoice("longPressMenuAction", ShortcutOptionCatalog::LongPress);
    verifySleepChoice("longPressBackAction", ShortcutOptionCatalog::LongPress);
    for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
      const auto side = std::find_if(sideButtonSettings.begin(), sideButtonSettings.end(),
                                     [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (side != sideButtonSettings.end()) {
        const auto sleep = std::find(side->enumRawValues.begin(), side->enumRawValues.end(), CrossPointSettings::SLEEP);
        if (sleep == side->enumRawValues.end() ||
            sideButtonOptionLabel(*side, static_cast<uint8_t>(sleep - side->enumRawValues.begin())) != tr(STR_SLEEP)) {
          fail("Sleep shortcut has the wrong label in %s", key);
        }
      }
    }
    if (gpio.hasHomeKey()) {
      verifyLibraryChoice("homeButtonTapAction", ShortcutOptionCatalog::HomeButton);
      verifyLibraryChoice("homeButtonDoubleTapAction", ShortcutOptionCatalog::HomeButton);
      verifyLibraryChoice("homeButtonLongPressAction", ShortcutOptionCatalog::HomeButton);
      verifySleepChoice("homeButtonTapAction", ShortcutOptionCatalog::HomeButton);
      verifySleepChoice("homeButtonDoubleTapAction", ShortcutOptionCatalog::HomeButton);
      verifySleepChoice("homeButtonLongPressAction", ShortcutOptionCatalog::HomeButton);
    }
    if (hasSideButtonChord) {
      const auto side =
          std::find_if(sideButtonSettings.begin(), sideButtonSettings.end(),
                       [](const SettingInfo& setting) { return settingKeyIs(setting, "sideButtonChordAction"); });
      if (side == sideButtonSettings.end() || !hasLibrary(*side, ShortcutOptionCatalog::ButtonChord)) {
        fail("Library shortcut is missing from the filtered Up + Down choices");
      }
      const auto sleep =
          std::find(side->enumRawValues.begin(), side->enumRawValues.end(), CrossPointSettings::CHORD_SLEEP);
      if (sleep == side->enumRawValues.end() ||
          sideButtonOptionLabel(*side, static_cast<uint8_t>(sleep - side->enumRawValues.begin())) != tr(STR_SLEEP)) {
        fail("Sleep shortcut is missing or mislabeled in the filtered Up + Down choices");
      }
    }
    if (!QuickActions::isQuickActionSlotActionAvailable(CrossPointSettings::LIBRARY) ||
        QuickActions::actionLabel(CrossPointSettings::LIBRARY) != StrId::STR_LIBRARY) {
      fail("Library is missing from Quick Actions choices");
    }

    const auto verifyChapterChoice = [&](const char* key, const uint8_t raw) {
      const auto setting = std::find_if(allSettings.begin(), allSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (setting == allSettings.end()) fail("Missing shortcut setting: %s", key);
      const auto choice = std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), raw);
      if (choice == setting->enumRawValues.end() ||
          setting->enumValues[static_cast<size_t>(choice - setting->enumRawValues.begin())] !=
              StrId::STR_SELECT_CHAPTER) {
        fail("Select Chapter is missing or mislabeled in %s", key);
      }
    };
    for (const char* key : {"shortPwrBtn", "longPwrBtn", "sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort",
                            "sideButtonDownLong"}) {
      verifyChapterChoice(key, CrossPointSettings::SELECT_CHAPTER);
    }
    verifyChapterChoice("powerChordAction", CrossPointSettings::CHORD_SELECT_CHAPTER);
    verifyChapterChoice("longPressMenuAction", CrossPointSettings::LONG_MENU_SELECT_CHAPTER);
    verifyChapterChoice("longPressBackAction", CrossPointSettings::LONG_MENU_SELECT_CHAPTER);
    if (hasSideButtonChord) verifyChapterChoice("sideButtonChordAction", CrossPointSettings::CHORD_SELECT_CHAPTER);
    if (gpio.hasHomeKey()) {
      for (const char* key : {"homeButtonTapAction", "homeButtonLongPressAction", "homeButtonDoubleTapAction"})
        verifyChapterChoice(key, CrossPointSettings::SELECT_CHAPTER);
    }
    if (gpio.hasTouch()) {
      for (const char* key : {"twoFingerSwipeUp", "twoFingerSwipeDown", "twoFingerSwipeLeft", "twoFingerSwipeRight",
                              "leftEdgeUp", "leftEdgeDown", "rightEdgeUp", "rightEdgeDown"})
        verifyChapterChoice(key, CrossPointSettings::TWO_FINGER_SWIPE_SELECT_CHAPTER);
    }
    if (!QuickActions::isQuickActionSlotActionAvailable(CrossPointSettings::SELECT_CHAPTER) ||
        QuickActions::actionLabel(CrossPointSettings::SELECT_CHAPTER) != StrId::STR_SELECT_CHAPTER) {
      fail("Select Chapter is missing from Quick Actions");
    }
    JsonDocument originalChapterSettings;
    SETTINGS.toJson(originalChapterSettings);
    JsonDocument chapterSettings;
    SETTINGS.toJson(chapterSettings);
    chapterSettings["shortPwrBtn"] = CrossPointSettings::SELECT_CHAPTER;
    chapterSettings["longPwrBtn"] = CrossPointSettings::SELECT_CHAPTER;
    chapterSettings["powerChordAction"] = CrossPointSettings::CHORD_SELECT_CHAPTER;
    chapterSettings["longPressMenuAction"] = CrossPointSettings::LONG_MENU_SELECT_CHAPTER;
    chapterSettings["longPressBackAction"] = CrossPointSettings::LONG_MENU_SELECT_CHAPTER;
    chapterSettings["quickActionSlots"][0] = CrossPointSettings::SELECT_CHAPTER;
    SETTINGS.fromJson(chapterSettings.as<JsonVariantConst>());
    if (SETTINGS.shortPwrBtn != CrossPointSettings::SELECT_CHAPTER ||
        SETTINGS.longPwrBtn != CrossPointSettings::SELECT_CHAPTER ||
        SETTINGS.powerChordAction != CrossPointSettings::CHORD_SELECT_CHAPTER ||
        SETTINGS.longPressMenuAction != CrossPointSettings::LONG_MENU_SELECT_CHAPTER ||
        SETTINGS.longPressBackAction != CrossPointSettings::LONG_MENU_SELECT_CHAPTER ||
        SETTINGS.quickActionSlots[0] != CrossPointSettings::SELECT_CHAPTER) {
      fail("Select Chapter settings did not survive reload");
    }
    SETTINGS.fromJson(originalChapterSettings.as<JsonVariantConst>());

    const uint8_t savedTrackReadingStats = SETTINGS.trackReadingStats;
    SETTINGS.trackReadingStats = 0;
    const auto statsDisabledSettings = getSettingsList();
    SETTINGS.trackReadingStats = savedTrackReadingStats;
    for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
      const auto setting = std::find_if(statsDisabledSettings.begin(), statsDisabledSettings.end(),
                                        [key](const SettingInfo& candidate) { return settingKeyIs(candidate, key); });
      if (setting == statsDisabledSettings.end() ||
          std::find(setting->enumRawValues.begin(), setting->enumRawValues.end(), CrossPointSettings::READING_STATS) !=
              setting->enumRawValues.end()) {
        fail("Reading Stats remains available in %s while tracking is disabled", key);
      }
    }
  }

  static void verifySideButtonMigrationAndInput() {
    JsonDocument original;
    SETTINGS.toJson(original);
    JsonDocument legacy;
    SETTINGS.toJson(legacy);
    for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
      legacy[key] = nullptr;
    }
    legacy["sideButtonLayout"] = CrossPointSettings::NEXT_PREV;
    legacy["sideButtonLongPress"] = CrossPointSettings::SIDE_LONG_CHAPTER_SKIP;
    SETTINGS.fromJson(legacy.as<JsonVariantConst>());
    if (SETTINGS.sideButtonUpShort != CrossPointSettings::PAGE_TURN ||
        SETTINGS.sideButtonDownShort != CrossPointSettings::PREVIOUS_PAGE ||
        SETTINGS.sideButtonUpLong != CrossPointSettings::SIDE_NEXT_CHAPTER ||
        SETTINGS.sideButtonDownLong != CrossPointSettings::SIDE_PREVIOUS_CHAPTER)
      fail("Swapped side-button layout migration failed");

    legacy["sideButtonLayout"] = CrossPointSettings::SIDE_BUTTONS_DISABLED;
    legacy["sideButtonLongPress"] = CrossPointSettings::SIDE_LONG_FONT_SIZE;
    SETTINGS.fromJson(legacy.as<JsonVariantConst>());
    if (SETTINGS.sideButtonUpShort != CrossPointSettings::IGNORE ||
        SETTINGS.sideButtonDownShort != CrossPointSettings::IGNORE ||
        SETTINGS.sideButtonUpLong != CrossPointSettings::SIDE_INCREASE_FONT ||
        SETTINGS.sideButtonDownLong != CrossPointSettings::SIDE_DECREASE_FONT)
      fail("Disabled layout with font hold migration failed");

    legacy["sideButtonUpShort"] = CrossPointSettings::LIBRARY;
    SETTINGS.fromJson(legacy.as<JsonVariantConst>());
    if (SETTINGS.sideButtonUpShort != CrossPointSettings::LIBRARY ||
        SETTINGS.sideButtonDownShort != CrossPointSettings::IGNORE)
      fail("Partially migrated side-button choices were overwritten");

    SETTINGS.fromJson(original.as<JsonVariantConst>());
    const auto sideSettings = buildControlsSideButtonSettingsList(getSettingsList());
    const size_t expected = 7 + (deviceSupportsSideButtonChord(gpio) ? 1u : 0u);
    if (sideSettings.size() != expected) fail("Side-button menu row count mismatch");
    for (const char* key : {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong"}) {
      if (std::find_if(sideSettings.begin(), sideSettings.end(),
                       [key](const SettingInfo& setting) { return settingKeyIs(setting, key); }) == sideSettings.end())
        fail("Side-button menu is missing an individual shortcut");
    }

    SideButtonShortcuts shortcuts;
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Up);
    const auto heldUp = shortcuts.update(mappedInputManager, 1000);
    if (!heldUp.consumed) fail("Side-button press was not captured");
    mappedInputManager.injectRelease(MappedInputManager::Button::Right);
    if (SideButtonShortcuts::shouldConsume(heldUp, mappedInputManager))
      fail("Side-button hold swallowed a chord page turn");
    mappedInputManager.clearInjectedReleases();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Up);
    const auto shortResult = shortcuts.update(mappedInputManager, 1200);
    if (!shortResult.triggered || shortResult.longPress || shortResult.action != SETTINGS.sideButtonUpShort)
      fail("Side-button short action did not dispatch once");
    mappedInputManager.simulatorClearInputFrame();
    if (shortcuts.update(mappedInputManager, 1300).triggered) fail("Side-button release dispatched twice");

    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Down);
    shortcuts.update(mappedInputManager, 2000);
    mappedInputManager.simulatorClearInputFrame();
    const auto longResult = shortcuts.update(mappedInputManager, 2800);
    if (!longResult.triggered || !longResult.longPress || longResult.action != SETTINGS.sideButtonDownLong)
      fail("Side-button long action did not dispatch");
    mappedInputManager.suppressNextSideRelease(MappedInputManager::Button::Down);
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
    if (mappedInputManager.wasReleased(MappedInputManager::Button::Down) ||
        shortcuts.update(mappedInputManager, 2900).triggered)
      fail("Side-button long release leaked into the next activity");
    mappedInputManager.simulatorClearInputFrame();
    if (shortcuts.update(mappedInputManager, 3000).triggered) fail("Side-button long action dispatched twice");

    ButtonShortcutController lock;
    lock.toggleQuickLock(4000, QuickLockTrigger::SideUpShort);
    if (lock.tryUnlockSide(4100, true, true, false, true, false, false, false, true, ReaderUtils::SKIP_HOLD_MS) ||
        !lock.isQuickLocked())
      fail("Side-button Quick Lock unlocked on press instead of release");
    if (!lock.tryUnlockSide(4200, false, false, true, true, false, false, false, true, ReaderUtils::SKIP_HOLD_MS) ||
        lock.isQuickLocked())
      fail("Side-button short Quick Lock did not unlock on a fresh release");
    lock.toggleQuickLock(5000, QuickLockTrigger::SideDownLong);
    if (lock.tryUnlockSide(5100, false, false, false, true, true, false, false, true, ReaderUtils::SKIP_HOLD_MS))
      fail("An inherited side-button hold unlocked Quick Lock");
    lock.tryUnlockSide(5200, false, false, false, true, false, false, true, true, ReaderUtils::SKIP_HOLD_MS);
    lock.tryUnlockSide(5300, false, false, false, true, true, true, false, true, ReaderUtils::SKIP_HOLD_MS);
    if (!lock.tryUnlockSide(6000, false, false, false, true, true, false, false, true, ReaderUtils::SKIP_HOLD_MS) ||
        lock.isQuickLocked())
      fail("Side-button long Quick Lock did not unlock on a fresh hold");
    LOG_INF("SMOKE", "Side-button migration and press/release checks passed");
  }

  [[noreturn]] static void fail(const char* message) {
    LOG_ERR("SMOKE", "%s", message);
    std::_Exit(2);
  }

  template <typename... Args>
  [[noreturn]] static void fail(const char* format, Args... args) {
    logPrintf("ERR", "SMOKE", format, args...);
    logPrintf("ERR", "SMOKE", "\n");
    std::_Exit(2);
  }

  static void renderCurrentStep(const char* name) {
    LOG_INF("SMOKE", "Rendering %s", name);
    if (activityManager.requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail("Render was rejected for %s", name);
    }
  }

  void queueStep(const char* name, SmokeStep nextStep, int framesToSettle = 3) {
    activeStepName = name;
    settleFrames = framesToSettle;
    step = nextStep;
  }

  void verifyStatusBarFeature() {
    JsonDocument original;
    SETTINGS.toJson(original);
    const auto originalOrientation = renderer.getOrientation();
    SETTINGS.clockDateHasBeenSynced = true;
    SETTINGS.dateFormat = CrossPointSettings::DATE_FORMAT_DAY_MONTH_YEAR_LONG;
    SETTINGS.displayStatusBar.slots = {ReaderStatusBarItem::Clock, ReaderStatusBarItem::Date,
                                       ReaderStatusBarItem::Battery};
    ReaderStatusBarConfig crowded = migrateBottomStatusBar({true, true, true, 1, 2, true, 2, 0, 2});
    crowded.slots[2] = ReaderStatusBarItem::Clock;
    SETTINGS.topReaderStatusBar = crowded;
    SETTINGS.bottomReaderStatusBar = crowded;
    RenderLock lock;
    const auto capture = [&](const std::string& name) { captureStatusBarScreen(name.c_str()); };
    const char* longTitle = "Les paramètres globaux très longs avec descendantes gypsy jumping";
    for (uint8_t theme = 0; theme < CrossPointSettings::UI_THEME_COUNT; ++theme) {
      if (theme == CrossPointSettings::COVER_GRID && !UITheme::supportsCoverGrid()) continue;
      SETTINGS.uiTheme = theme;
      UITheme::getInstance().reload();
      for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                     GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
        renderer.setOrientation(orientation);
        for (uint8_t uiScale = 0; uiScale < CrossPointSettings::UI_SCALE_COUNT; ++uiScale) {
          SETTINGS.uiScale = uiScale;
          const auto stem =
              "theme-" + std::to_string(theme) + "-o-" + std::to_string(orientation) + "-ui-" + std::to_string(uiScale);
          for (uint8_t size = 0; size < 3; ++size) {
            SETTINGS.displayStatusBarTextSize = size;
            const auto& metrics = UITheme::getInstance().getMetrics();
            const auto name = stem + "-global-" + std::to_string(size);
            if (UITheme::getDisplayStatusBarTextHeight(renderer) > 19 + UITheme::getDisplayStatusBarHeightIncrease())
              fail("Global metric enlargement is smaller than the measured font lane");
            const int readerHeight = UITheme::getReaderStatusBarHeight(ReaderStatusBarPosition::Bottom, renderer);
            renderer.clearScreen();
            GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                           longTitle);
            capture(name + "-full");
            renderer.clearScreen();
            if (mappedInputManager.hasTouchHardware())
              TouchHeaderBackButton::drawCompact(renderer, longTitle, false, true);
            else
              CompactHeader::drawTitle(renderer, longTitle, true);
            capture(name + "-compact");
            // The first content row is outlined to make overlap visible in the captures.
            renderer.clearScreen();
            if (mappedInputManager.hasTouchHardware())
              TouchHeaderBackButton::drawCompact(renderer, longTitle, false, true);
            else
              CompactHeader::drawTitle(renderer, longTitle, true);
            const int contentTop = CompactHeader::contentTop(metrics);
            renderer.drawRect(0, contentTop, renderer.getScreenWidth(), 38);
            capture(name + "-content");
            // The clock can change minutes during this large matrix. Keep
            // captures complete, but compare deterministic date/battery pixels.
            const auto clockSlot = SETTINGS.displayStatusBar.slots[0];
            SETTINGS.displayStatusBar.slots[0] = ReaderStatusBarItem::Empty;
            renderer.clearScreen();
            if (mappedInputManager.hasTouchHardware())
              TouchHeaderBackButton::drawCompact(renderer, longTitle, false, true);
            else
              CompactHeader::drawTitle(renderer, longTitle, true);
            const auto globalHash = hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize());
            for (uint8_t readerSize = 0; readerSize < 3; ++readerSize) {
              SETTINGS.statusBarTextSize = readerSize;
              renderer.clearScreen();
              if (mappedInputManager.hasTouchHardware())
                TouchHeaderBackButton::drawCompact(renderer, longTitle, false, true);
              else
                CompactHeader::drawTitle(renderer, longTitle, true);
              if (globalHash != hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize()))
                fail("Reader text size changed global header pixels");
            }
            SETTINGS.displayStatusBar.slots[0] = clockSlot;
            SETTINGS.statusBarTextSize = 0;
            if (readerHeight != UITheme::getReaderStatusBarHeight(ReaderStatusBarPosition::Bottom, renderer))
              fail("Global text size changed the reader's reserved space");
            if (mappedInputManager.hasTouchHardware()) {
              const auto header = TouchHeaderBackButton::compactHeaderRect(renderer);
              const auto target = TouchHeaderBackButton::layout(header);
              if (target.touchRect.height < 68 || target.iconRect.y + target.iconRect.height > header.y + header.height)
                fail("Enlarged global status strip broke the Back/search action target");
            }
            if (uiScale == CrossPointSettings::UI_SCALE_LARGE) {
              renderer.clearScreen();
              const GlobalReadingStats stats{};
              renderGlobalStatsPage(renderer, &mappedInputManager, tr(STR_READING_STATS), stats, true, false);
              capture(name + "-stats");
              const BookReadingStats bookStats{};
              renderPerBookStatsPage(renderer, &mappedInputManager, longTitle, bookStats, 75, false, 0, true, true,
                                     true);
              capture(name + "-book-stats");
              if (!halClock.isAvailable()) {
                renderNoRtcCombinedStatsPage(renderer, &mappedInputManager, longTitle, bookStats, 75, false, 0, stats,
                                             &stats, true);
                capture(name + "-combined-stats");
              }
              renderEditBookDatesPage(renderer, &mappedInputManager, longTitle, bookStats, 0, true);
              capture(name + "-edit-dates");
            }
          }
        }
      }
    }
    // Cached languages stay pinned until reboot. Run the same render matrix
    // for each language through the runner's --language-file boot fixture.
    const auto language = I18N.getLanguage();
    for (uint8_t theme = 0; theme < CrossPointSettings::UI_THEME_COUNT; ++theme) {
      if (theme == CrossPointSettings::COVER_GRID && !UITheme::supportsCoverGrid()) continue;
      SETTINGS.uiTheme = theme;
      UITheme::getInstance().reload();
      for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                     GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
        renderer.setOrientation(orientation);
        for (const uint8_t size : {0, 2}) {
          SETTINGS.displayStatusBarTextSize = size;
          if (UITheme::getDisplayStatusBarTextHeight(renderer) > 19 + UITheme::getDisplayStatusBarHeightIncrease())
            fail("Localized status text exceeds the global font lane");
          renderer.clearScreen();
          if (mappedInputManager.hasTouchHardware())
            TouchHeaderBackButton::drawCompact(renderer, tr(STR_STATUS_BAR_TEXT_SIZE), false, true);
          else
            CompactHeader::drawTitle(renderer, tr(STR_STATUS_BAR_TEXT_SIZE), true);
          capture("localized-lang-" + std::to_string(static_cast<int>(language)) + "-theme-" + std::to_string(theme) +
                  "-o-" + std::to_string(orientation) + "-global-" + std::to_string(size));
        }
      }
    }

    SETTINGS.uiTheme = CrossPointSettings::LYRA;
    UITheme::getInstance().reload();
    SETTINGS.uiScale = CrossPointSettings::UI_SCALE_LARGE;
    for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted,
                                   GfxRenderer::LandscapeClockwise, GfxRenderer::LandscapeCounterClockwise}) {
      renderer.setOrientation(orientation);
      for (uint8_t size = 0; size < 3; ++size) {
        SETTINGS.statusBarTextSize = size;
        for (unsigned mask = 0; mask < 4; ++mask) {
          SETTINGS.topReaderStatusBar.hidden = mask & 1;
          SETTINGS.bottomReaderStatusBar.hidden = mask & 2;
          const auto topSlots = SETTINGS.topReaderStatusBar.slots;
          const auto bottomSlots = SETTINGS.bottomReaderStatusBar.slots;
          JsonDocument persisted;
          SETTINGS.toJson(persisted);
          if (!SETTINGS.saveToFile()) fail("Cannot persist status bar feature settings");
          SETTINGS.topReaderStatusBar.hidden = false;
          SETTINGS.bottomReaderStatusBar.hidden = false;
          SETTINGS.displayStatusBarTextSize = 0;
          if (!SETTINGS.loadFromFile()) fail("Cannot reload status bar feature settings");
          if (SETTINGS.topReaderStatusBar.hidden != bool(mask & 1) ||
              SETTINGS.bottomReaderStatusBar.hidden != bool(mask & 2) ||
              SETTINGS.topReaderStatusBar.slots != topSlots || SETTINGS.bottomReaderStatusBar.slots != bottomSlots ||
              SETTINGS.statusBarTextSize != size || SETTINGS.displayStatusBarTextSize != 2)
            fail("Independent visibility/sizes/slots failed disk persistence");
          if ((mask & 1) && ReaderUtils::getTopStatusBarReservedHeight(renderer))
            fail("Hidden top still reserves reader space");
          if ((mask & 2) &&
              (UITheme::getStatusBarHeight(renderer) || UITheme::getProgressBarHeight() ||
               ReaderUtils::getReaderFooterReservedHeight(renderer, true) != SETTINGS.screenMarginVertical))
            fail("Hidden bottom still reserves status or auto-turn space");
          renderer.clearScreen();
          ReaderStatusBarContent content;
          content.previewClock = "12:34";
          content.chapterTitle = longTitle;
          content.timeLeftBook = "3h 40m";
          content.chapterPage = 888;
          content.chapterPageCount = 999;
          content.stablePage = 1234;
          content.stablePageCount = 9999;
          content.bookProgress = 75.12f;
          content.bookmarked = true;
          content.autoTurnLabel = "Auto 20s";
          GUI.drawReaderStatusBar(renderer, ReaderStatusBarPosition::Top, content);
          GUI.drawReaderStatusBar(renderer, ReaderStatusBarPosition::Bottom, content);
          capture("reader-o-" + std::to_string(orientation) + "-size-" + std::to_string(size) + "-hidden-" +
                  std::to_string(mask));
          if (mask == 3) {
            const auto* pixels = renderer.getFrameBuffer();
            if (!std::all_of(pixels, pixels + renderer.getBufferSize(), [](uint8_t b) { return b == 0xff; }))
              fail("Hidden bars drew progress, bookmark or auto-turn pixels");
          }
        }
      }
    }
    renderer.setOrientation(GfxRenderer::Portrait);
    if (!StatusBarFeatureSmokeTest::settings(lock, capture)) fail("Status bar popup controls failed");
    renderer.setOrientation(GfxRenderer::Portrait);
    if (!StatusBarFeatureSmokeTest::txt(lock, capture)) fail("TXT hide/show offset or reopen failed");
    if (!StatusBarFeatureSmokeTest::xtc(lock, capture)) fail("XTC bitmap hide/show failed");
    JsonDocument invalid;
    SETTINGS.toJson(invalid);
    invalid["displayStatusBarTextSize"] = 99;
    SETTINGS.fromJson(invalid.as<JsonVariantConst>());
    if (SETTINGS.displayStatusBarTextSize != 0) fail("Invalid global status size accepted");
    invalid.remove("displayStatusBarTextSize");
    SETTINGS.fromJson(invalid.as<JsonVariantConst>());
    if (SETTINGS.displayStatusBarTextSize != 0) fail("Old settings did not default global size to Small");
    SETTINGS.fromJson(original.as<JsonVariantConst>());
    renderer.setOrientation(originalOrientation);
    UITheme::getInstance().reload();
    renderer.clearScreen();
    LOG_INF("SMOKE", "Status bar feature render matrix, independent sizes, hidden geometry and disk reload passed");
  }

  void verifyStatusBarTextSizes() {
    JsonDocument original;
    SETTINGS.toJson(original);
    const auto originalOrientation = renderer.getOrientation();
    const ReaderStatusBarConfig empty{};
    ReaderStatusBarConfig crowded;
    crowded.slots = {ReaderStatusBarItem::Battery,
                     ReaderStatusBarItem::TimeLeftBook,
                     ReaderStatusBarItem::Clock,
                     ReaderStatusBarItem::TitleChapter,
                     ReaderStatusBarItem::ChapterPageCount,
                     ReaderStatusBarItem::StablePageNumber,
                     ReaderStatusBarItem::BookProgressPercentage};
    ReaderStatusBarContent content;
    content.previewOriginY = 100;
    content.previewClock = "12:34";
    content.chapterTitle = "A long chapter title with descenders: gypsy jumping";
    content.timeLeftBook = "3h 40m";
    content.chapterPage = 888;
    content.chapterPageCount = 999;
    content.stablePage = 1234;
    content.stablePageCount = 9999;
    content.bookProgress = 75.12f;
    content.bookmarked = true;
    RenderLock lock;
    for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise}) {
      renderer.setOrientation(orientation);
      int previousHeight = 0;
      uint64_t displayBarHash = 0;
      for (uint8_t size = 0; size < 3; ++size) {
        SETTINGS.statusBarTextSize = size;
        JsonDocument saved;
        SETTINGS.toJson(saved);
        SETTINGS.statusBarTextSize = 0;
        SETTINGS.fromJson(saved.as<JsonVariantConst>());
        if (SETTINGS.statusBarTextSize != size) fail("Status bar text size did not survive settings round trip");
        const int height = UITheme::getReaderStatusBarTextHeight(renderer);
        if (height <= previousHeight) fail("Larger status bar text did not reserve more reading space");
        previousHeight = height;
        // Display headers share the drawing path but keep their existing text size.
        ReaderStatusBarContent displayContent;
        displayContent.outsideReader = true;
        displayContent.previewOriginY = 100;
        displayContent.previewClock = "12:34";
        ReaderStatusBarConfig displayConfig;
        displayConfig.slots[0] = ReaderStatusBarItem::Clock;
        displayConfig.slots[6] = ReaderStatusBarItem::Battery;
        renderer.clearScreen();
        GUI.drawReaderStatusBar(renderer, ReaderStatusBarPosition::Top, displayContent, &displayConfig);
        const uint64_t currentDisplayBarHash = hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize());
        if (size == 0)
          displayBarHash = currentDisplayBarHash;
        else if (currentDisplayBarHash != displayBarHash)
          fail("Reader text size changed the Display status bar");
        SETTINGS.topReaderStatusBar = empty;
        SETTINGS.bottomReaderStatusBar = empty;
        if (UITheme::getStatusBarHeight(renderer) ||
            UITheme::getReaderStatusBarHeight(ReaderStatusBarPosition::Top, renderer))
          fail("Text size reserved space for an empty status bar");
        if (ReaderUtils::getReaderFooterReservedHeight(renderer, true) < height + ReaderUtils::STATUS_BAR_TEXT_PADDING)
          fail("Automatic page turn did not reserve its enlarged label");
        SETTINGS.topReaderStatusBar = crowded;
        SETTINGS.bottomReaderStatusBar = crowded;
        for (const auto position : {ReaderStatusBarPosition::Top, ReaderStatusBarPosition::Bottom}) {
          renderer.clearScreen();
          GUI.drawReaderStatusBar(renderer, position, content);
          // Verify the text stays above the reading area (including its existing padding).
          const int end = content.previewOriginY + UITheme::getReaderStatusBarHeight(position, renderer) +
                          ReaderUtils::STATUS_BAR_TEXT_PADDING;
          // Erase the allowed rectangle in logical coordinates, so partial framebuffer
          // bytes at a rotated boundary cannot look like text outside the bar.
          renderer.fillRect(0, 0, renderer.getScreenWidth(), end, false);
          const uint8_t* pixels = renderer.getFrameBuffer();
          if (!std::all_of(pixels, pixels + renderer.getBufferSize(), [](uint8_t byte) { return byte == 0xff; }))
            fail("Status bar text escaped its reserved reading space: size=%u orientation=%u position=%u end=%d", size,
                 static_cast<unsigned>(orientation), static_cast<unsigned>(position), end);
        }
      }
    }
    JsonDocument invalid;
    SETTINGS.toJson(invalid);
    invalid["statusBarTextSize"] = 99;
    SETTINGS.statusBarTextSize = 0;
    SETTINGS.fromJson(invalid.as<JsonVariantConst>());
    if (SETTINGS.statusBarTextSize != 0) fail("Invalid status bar text size was not rejected");
    invalid.remove("statusBarTextSize");
    SETTINGS.fromJson(invalid.as<JsonVariantConst>());
    if (SETTINGS.statusBarTextSize != 0) fail("Legacy settings did not retain Small status bar text");
    SETTINGS.fromJson(original.as<JsonVariantConst>());
    renderer.setOrientation(originalOrientation);
    renderer.clearScreen();
    LOG_INF("SMOKE", "Status bar text sizes, persistence, empty bars, auto-turn and crowded layout passed");
  }

  void verifyLoadingPopupBackdrop() {
    RenderLock lock;
    const auto originalOrientation = renderer.getOrientation();
    for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                   GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise}) {
      renderer.setOrientation(orientation);
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      renderer.clearScreen();
      for (int y = 0; y < height; y += 7) renderer.drawLine(0, y, width - 1, y);
      const size_t bytes = renderer.getRegionByteSize(0, 0, width, height);
      std::vector<uint8_t> before(bytes), after(bytes);
      if (!renderer.copyRegionToBuffer(0, 0, width, height, before.data(), bytes))
        fail("Could not snapshot loading popup backdrop");
      GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), true);
      if (!renderer.copyRegionToBuffer(0, 0, width, height, after.data(), bytes) || before != after)
        fail("Loading popup changed the underlying framebuffer");
    }
    renderer.setOrientation(originalOrientation);
    renderer.clearScreen();
    LOG_INF("SMOKE", "Loading popup preserves backdrop in all orientations");
  }

  static void verifyWakePowerReaderShortcut() {
    if (!activityManager.isCurrentActivityNamed("EpubReader")) return;
    auto* reader = activityManager.simulatorCurrentActivity();
    const uint8_t savedShort = SETTINGS.shortPwrBtn;
    const uint8_t savedLong = SETTINGS.longPwrBtn;
    const uint8_t savedSlot = SETTINGS.quickActionSlots[0];
    SETTINGS.shortPwrBtn = CrossPointSettings::SHORT_PWRBTN::QUICK_LOCK;
    SETTINGS.longPwrBtn = CrossPointSettings::SHORT_PWRBTN::QUICK_ACTIONS;
    SETTINGS.quickActionSlots[0] = CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH;

    // Apply the same mapped-input guards as a Power-button wake, then exercise
    // the reader's independent long-press route across the hold and release.
    mappedInputManager.suppressNextPowerRelease();
    mappedInputManager.suppressNextPowerConfirmRelease();
    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Power);
    mappedInputManager.update();
    delay(SETTINGS.getPowerButtonLongPressDuration() + 10);
    if (!mappedInputManager.isPhysicalPressed(MappedInputManager::Button::Power)) fail("Wake test lost Power hold");
    reader->loop();
    if (reader->blocksGlobalInput()) fail("Wake Power hold opened Quick Actions");

    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Power);
    mappedInputManager.update();
    reader->loop();
    if (reader->blocksGlobalInput()) fail("Wake Power release opened Quick Actions");
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.update();

    mappedInputManager.simulatorInjectPress(MappedInputManager::Button::Power);
    mappedInputManager.update();
    delay(SETTINGS.getPowerButtonLongPressDuration() + 10);
    reader->loop();
    if (!reader->blocksGlobalInput()) fail("Fresh long Power press did not open Quick Actions");
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Power);
    mappedInputManager.update();
    reader->loop();
    mappedInputManager.simulatorClearInputFrame();
    mappedInputManager.update();
    mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Back);
    reader->loop();
    mappedInputManager.simulatorClearInputFrame();
    if (reader->blocksGlobalInput()) fail("Wake test could not close Quick Actions");
    SETTINGS.shortPwrBtn = savedShort;
    SETTINGS.longPwrBtn = savedLong;
    SETTINGS.quickActionSlots[0] = savedSlot;
    LOG_INF("SMOKE", "Wake Power hold/release ignored; fresh long press opens Quick Actions");
  }

  static void verifyCachedHomeProgressMigration() {
    const RecentBook book{"/books/legacy-home-smoke.epub", "Legacy Home smoke", {}, {}};
    const std::string legacy = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(book.path));
    const std::string current = Epub::cachePathForFilePath(book.path, "/.crosspoint");
    if (legacy == current || Storage.exists(book.path.c_str())) fail("Invalid legacy Home fixture");
    if (!Storage.mkdir(legacy.c_str())) fail("Cannot create legacy Home cache");
    RecentBookProgress::saveCachedEpubPercent(legacy, 42.5f);
    // There is deliberately no EPUB or book.bin. This must migrate and read the
    // tiny saved percentage without attempting to open, parse or index a book.
    if (RecentBookProgress::loadCachedEpubPercent(book) != 42.5f || Storage.exists(legacy.c_str()) ||
        !Storage.exists(current.c_str()) || BookMetadataCache::exists(current))
      fail("Home did not recover legacy cached progress without opening the EPUB");
    const std::string percentFile = current + "/progress_percent.bin";
    const std::string hostPercentFile = "fs_" + percentFile;
    // Force an old timestamp so this check does not depend on filesystem clock resolution.
    const auto oldTime = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
    std::filesystem::last_write_time(hostPercentFile, oldTime);
    const auto savedTime = std::filesystem::last_write_time(hostPercentFile);
    RecentBookProgress::saveCachedEpubPercent(current, 42.5001f);
    if (std::filesystem::last_write_time(hostPercentFile) != savedTime)
      fail("Saving the same rounded Home percentage rewrote the cache");
    RecentBookProgress::saveCachedEpubPercent(current, 43.5f);
    if (RecentBookProgress::loadCachedEpubPercent(book) != 43.5f) fail("Changed Home percentage was not saved");
    std::filesystem::resize_file(hostPercentFile, 2);
    RecentBookProgress::saveCachedEpubPercent(current, 43.5f);
    if (RecentBookProgress::loadCachedEpubPercent(book) != 43.5f || std::filesystem::file_size(hostPercentFile) != 7)
      fail("Saving Home percentage did not repair a truncated cache");
    if (!Storage.remove(percentFile.c_str())) fail("Cannot remove Home percent fixture");
    RecentBookProgress::saveCachedEpubPercent(current, 42.5f);
    LOG_INF("SMOKE", "Home percentage skips unchanged writes and repairs missing/truncated caches");
    if (!Storage.mkdir(legacy.c_str())) fail("Cannot recreate stale legacy Home cache");
    RecentBookProgress::saveCachedEpubPercent(legacy, 90.0f);
    if (RecentBookProgress::loadCachedEpubPercent(book) != 42.5f || !Storage.exists(legacy.c_str()))
      fail("Stale legacy progress replaced the current Home cache");
    if (!Storage.removeDir(legacy.c_str()) || !Storage.removeDir(current.c_str())) fail("Cannot remove Home fixtures");
    LOG_INF("SMOKE", "Legacy Home progress migration without EPUB loading passed");
  }

  static uint64_t hashBytes(const uint8_t* bytes, size_t size, uint64_t hash = 14695981039346656037ull) {
    for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= 1099511628211ull;
    }
    return hash;
  }

  static uint64_t hashCarouselCache() {
    FsFile file;
    if (!Storage.openFileForRead("SMOKE", "/.crosspoint/home_carousel_cache_0.bin", file)) {
      fail("Carousel did not publish an artwork cache");
    }
    if (file.size() < renderer.getBufferSize() || file.size() >= 2 * renderer.getBufferSize()) {
      file.close();
      fail("Carousel snapshot must contain only one viewed artwork frame");
    }
    uint64_t hash = 14695981039346656037ull;
    uint8_t bytes[128];
    int count;
    while ((count = file.read(bytes, sizeof(bytes))) > 0) hash = hashBytes(bytes, count, hash);
    file.close();
    return hash;
  }

  void queueCarouselSwipe(bool next) {
    ++carouselCachePass;
    inputScript.clear();
    scriptIndex = 0;
    inputCompletionStep = SmokeStep::CarouselHome;
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouchHardware()) {
      const int startX = renderer.getScreenWidth() * (next ? 3 : 1) / 4;
      const int endX = renderer.getScreenWidth() * (next ? 1 : 3) / 4;
      const int y = renderer.getScreenHeight() / 2;
      inputScript.push_back(touchDown(startX, y));
      inputScript.push_back(touchMove(endX, y));
      inputScript.push_back(touchRelease(endX, y));
    } else
#endif
    {
      addTap(next ? MappedInputManager::Button::Right : MappedInputManager::Button::Left);
    }
    inputScript.push_back(render("Carousel viewed position", 8));
    step = SmokeStep::ReaderInput;
  }

  void verifyCarouselCacheReturn() {
    const auto writtenAt = std::filesystem::last_write_time("fs_/.crosspoint/home_carousel_cache_0.bin");
    const uint64_t cacheHash = hashCarouselCache();
    uint64_t screenHash;
    {
      RenderLock lock;
      screenHash = hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize());
    }
    if (carouselCachePass == 0) {
      if (Storage.exists("/.crosspoint/home_carousel_cache_1.bin") ||
          Storage.exists("/.crosspoint/home_carousel_cache_2.bin")) {
        fail("Carousel eagerly prepared unviewed positions");
      }
      carouselCacheWrittenAt = writtenAt;
      carouselCacheHash = cacheHash;
      const RecentBook& book = RECENT_BOOKS.getBooks().front();
      const std::string cachePath = Epub::resolveCachePathForFilePath(book.path, "/.crosspoint");
      const float oldProgress = RecentBookProgress::loadCachedEpubPercent(book);
      RecentBookProgress::saveCachedEpubPercent(cachePath, oldProgress < 50.0f ? 75.0f : 25.0f);
      BookReadingStats stats = BookReadingStats::load(cachePath);
      stats.sessionCount = 1;
      stats.totalReadingSeconds += 7200;
      if (!stats.save(cachePath)) fail("Cannot save carousel stats fixture");
    } else if (carouselCachePass <= 2) {
      if (writtenAt != carouselCacheWrittenAt || cacheHash != carouselCacheHash) {
        fail("Reading progress/stats or tracking settings rebuilt the carousel artwork");
      }
      if (screenHash == carouselScreenHash) fail("Carousel restored stale progress/stats or menu pixels");
      if (carouselCachePass == 1) {
        SETTINGS.trackReadingStats = 0;
      } else {
        SETTINGS.screenInverted = !SETTINGS.screenInverted;
      }
    } else if (carouselCachePass == 3) {
      if (cacheHash == carouselCacheHash) fail("Dark Mode did not invalidate carousel artwork");
      carouselCacheHash = cacheHash;
      const RecentBook book = RECENT_BOOKS.getBooks().front();
      if (!RECENT_BOOKS.updateBook(book.path, "Changed carousel title", book.author, book.coverBmpPath,
                                   book.coverState))
        fail("Cannot update carousel title fixture");
    } else if (carouselCachePass == 4) {
      if (cacheHash == carouselCacheHash) fail("Title changes did not invalidate carousel artwork");
      carouselCacheHash = cacheHash;
      carouselCacheWrittenAt = writtenAt;
      queueCarouselSwipe(true);
      return;
    } else {
      if (cacheHash != carouselCacheHash || writtenAt != carouselCacheWrittenAt) {
        fail("Navigating the carousel rewrote an already cached position");
      }
      const bool secondExists = Storage.exists("/.crosspoint/home_carousel_cache_1.bin");
      if (!secondExists || Storage.exists("/.crosspoint/home_carousel_cache_2.bin")) {
        fail("Carousel navigation did not cache only the viewed position");
      }
      const auto secondWrittenAt = std::filesystem::last_write_time("fs_/.crosspoint/home_carousel_cache_1.bin");
      const int selected = carouselCachePass == 5 ? 1 : 0;
      if (activityManager.getCurrentBookPath() != RECENT_BOOKS.getBooks()[selected].path) {
        fail("Carousel swipe did not select the expected book");
      }
      if (carouselCachePass == 5) {
        carouselSecondWrittenAt = secondWrittenAt;
        queueCarouselSwipe(false);
        return;
      }
      if (secondWrittenAt != carouselSecondWrittenAt) fail("Carousel rewrote the previous position while leaving it");
      LOG_INF("SMOKE", "Carousel lazy cache, live progress/stats/menu, Dark Mode and title invalidation passed");
      step = SmokeStep::Done;
      return;
    }
    carouselScreenHash = screenHash;
    ++carouselCachePass;
    activityManager.goHome();
    queueStep("Carousel return cache", SmokeStep::CarouselHome, 8);
  }

#if CROSSINK_SCALABLE_FONTS
  void verifyBlockFontSizes(const int readerFontId) {
    RenderLock lock;
    const int baseFont = renderer.getFontIdForSize(readerFontId, 12);
    if (renderer.getFontPointSize(baseFont) != 12) fail("Missing scalable body size");
    for (const uint8_t points : {8, 12, 24, 32, 44}) {
      if (renderer.getFontPointSize(renderer.getFontIdForSize(baseFont, points)) != points)
        fail("Missing scalable content size %u", unsigned(points));
    }
    const std::string path = "/block-font-smoke.xhtml";
    if (!Storage.writeFile(path.c_str(),
                           "<html><body><h1>Heading Heading Heading Heading Heading</h1>"
                           "<p style=\"font-size:150%\">Large Large Large</p><p>Body Body Body</p></body></html>"))
      fail("Cannot write block font fixture");
    Epub book("/block-font-smoke.epub", "/.crosspoint");
    CssParser css("/.crosspoint/block-font-smoke");
    unsigned checked = 0;
    ChapterHtmlSlimParser parser(
        book, path, renderer, baseFont, 1.0f, false, false, 1, 320, 600, false, false, false, 0,
        [&](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t, uint32_t) {
          FsFile file;
          if (!Storage.openFileForWrite("SMOKE", "/block-font-page", file) || !page->serialize(file))
            fail("Cannot save sized page");
          file.close();
          if (!Storage.openFileForRead("SMOKE", "/block-font-page", file)) fail("Cannot reload sized page");
          auto restored = Page::deserialize(file);
          file.close();
          if (!restored) fail("Sized page cache did not round trip");
          int bottom = 0;
          for (const auto& element : restored->elements) {
            if (element->getTag() != TAG_PageLine) continue;
            const auto& line = static_cast<const PageLine&>(*element);
            const auto& block = *line.getBlock();
            if (!block.wordCount()) continue;
            const char* word = block.wordText(0);
            const int expected = std::strcmp(word, "Heading") == 0 ? 24 : std::strcmp(word, "Large") == 0 ? 18 : 12;
            const int actualFont = block.resolvedFontId(renderer, baseFont);
            if (block.getBlockStyle().fontSize != expected || renderer.getFontPointSize(actualFont) != expected ||
                block.getBlockStyle().lineHeight != renderer.getLineHeight(actualFont))
              fail("Sized page metrics mismatch: %s", word);
            if (line.yPos < bottom || line.yPos + block.getBlockStyle().lineHeight > 600)
              fail("Sized page lines overlap or overflow");
            bottom = line.yPos + block.getBlockStyle().lineHeight;
            ++checked;
          }
          restored->renderText(renderer, baseFont, 0, 0);
        },
        true, "", "", 0, {}, nullptr, &css);
    if (!parser.parseAndBuildPages() || checked < 3) fail("Block font parser smoke failed");
    WordSelectNavigator navigator;
    WordSelectNavigator::WordInfo word;
    word.textLen = word.lookupLen = 7;
    word.screenX = 20;
    word.screenY = 20;
    word.width = 100;
    word.setLineHeight(60);
    word.fontId = renderer.getFontIdForSize(baseFont, 24);
    navigator.load({word}, {{20, 0, 1}}, "Heading", false);
    bool hit = false;
    navigator.selectWordAtPoint(50, 75, 20, &hit);
    if (!hit) fail("Heading word selection used body height");
    Storage.remove(path.c_str());
    Storage.remove("/block-font-page");
    LOG_INF("SMOKE", "Block font sizes: layout, cache, rendering and selection passed");
  }
#endif

  void tickFileBrowserSyncReturn() {
#if CROSSINK_APP_CAP_TOUCH
    const char* contextMenu = std::getenv("CROSSINK_SIMULATOR_SMOKE_CONTEXT_MENU_SYNC_RETURN");
    const bool fromLibrary = contextMenu && std::string_view(contextMenu) == "library";
    const char* phase = std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE");
    if (!phase) {
      const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
      if (!bookPath || !Storage.exists(bookPath)) fail("Browser sync book fixture missing");
      for (int row = 0; row < 24; ++row) {
        const std::string path = "/books/a-sync-fixture-" + std::to_string(row) + ".txt";
        if (!Storage.writeFile(path.c_str(), "Fixture")) fail("Cannot create browser row fixture");
      }
      APP_STATE.openEpubPath = bookPath;
      if (!APP_STATE.saveToFile()) fail("Cannot save browser sync book path");
      KOREADER_STORE.setCredentials("smoke", "smoke");
      if (!KOREADER_STORE.saveToFile()) fail("Cannot save browser sync credentials");
      if (fromLibrary) {
        RECENT_BOOKS.addOrUpdateBook(bookPath, "Sync EPUB", "", "");
        for (int row = 0; row < 12; ++row) {
          const std::string path = "/books/a-sync-fixture-" + std::to_string(row) + ".txt";
          RECENT_BOOKS.addOrUpdateBook(path, "Sync fixture", "", "");
        }
        if (!RECENT_BOOKS.saveToFile()) fail("Cannot save Library sync fixture");
        activityManager.goToLibrary();
      } else {
        activityManager.goToFileBrowser(bookPath);
      }
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "launch", 1);
      settleFrames = 4;
      return;
    }
    if (std::string_view(phase) == "launch" && fromLibrary) {
      auto* library = dynamic_cast<LibraryActivity*>(activityManager.simulatorCurrentActivity());
      if (!library) fail("Expected Library before menu sync");
      {
        RenderLock lock;
        library->simulatorSetView(4, true, "Sync");
        library->simulatorSelectRow(12);
        // Restore the view even if saved global preferences differ.
        SETTINGS.librarySortMethod = 1;
        SETTINGS.librarySortDescending = false;
        if (!SETTINGS.saveToFile()) fail("Cannot save Library sync settings fixture");
        library->requestUpdate();
      }
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "library-view", 1);
      settleFrames = 4;
      return;
    }
    if (std::string_view(phase) == "library-view") {
      auto* library = dynamic_cast<LibraryActivity*>(activityManager.simulatorCurrentActivity());
      if (!library) fail("Expected settled Library before menu sync");
      {
        RenderLock lock;
        const std::string selection = std::to_string(library->simulatorSelection());
        const std::string scroll = std::to_string(library->simulatorTopIndex());
        setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SELECTION", selection.c_str(), 1);
        setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SCROLL", scroll.c_str(), 1);
        library->simulatorOpenContextMenu();
      }
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "drawer", 1);
      inputScript = {render("Library context menu opened", 4), assertActivity("FileBrowserAction")};
      addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      inputScript.push_back(press(MappedInputManager::Button::Confirm));
      scriptIndex = 0;
      return;
    }
    if (std::string_view(phase) == "launch") {
      auto* browser = dynamic_cast<FileBrowserActivity*>(activityManager.simulatorCurrentActivity());
      if (!browser || browser->simulatorFolderPath() != "/books" || browser->simulatorSelectedIndex() == 0)
        fail("Expected a selected book inside its folder before sync");
      const std::string selection = std::to_string(browser->simulatorSelectedIndex());
      const std::string scroll = std::to_string(browser->simulatorTopIndex());
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SELECTION", selection.c_str(), 1);
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SCROLL", scroll.c_str(), 1);
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "drawer", 1);
      if (contextMenu) {
        {
          RenderLock lock;
          browser->simulatorOpenContextMenu();
        }
        inputScript = {render("Browser context menu opened", 4), assertActivity("FileBrowserAction")};
        for (int row = 0; row < 3; ++row) addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        inputScript.push_back(press(MappedInputManager::Button::Confirm));
        scriptIndex = 0;
        return;
      }
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      inputScript = {touchDown(width / 2, 8),
                     touchMove(width / 2, height / 4),
                     touchRelease(width / 2, height / 4),
                     render("Browser sync drawer opened", 4),
                     {ScriptActionType::OpenFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 0, 0},
                     render("Browser sync choices opened", 4),
                     press(MappedInputManager::Button::Confirm)};
      scriptIndex = 0;
      return;
    }
    if (std::string_view(phase) == "drawer") {
      if (scriptIndex + 1 == inputScript.size())
        setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "network", 1);
      runReaderInputScript();
      return;
    }
    if (std::string_view(phase) == "network") {
      if (!activityManager.isCurrentActivityNamed("WifiSelection")) fail("Expected Wi-Fi selection after reboot");
      const auto expectedOrigin = fromLibrary ? PendingOverlayOrigin::Library : PendingOverlayOrigin::FileBrowser;
      if (APP_STATE.pendingOverlayResume.origin != expectedOrigin ||
          (!fromLibrary && APP_STATE.pendingOverlayResume.fileBrowserPath != "/books") ||
          (fromLibrary && APP_STATE.pendingOverlayResume.libraryQuery != "Sync"))
        fail("Book list return route was lost across network reboot");
      setenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_PHASE", "returned", 1);
      inputScript = {press(MappedInputManager::Button::Back), release(MappedInputManager::Button::Back)};
      scriptIndex = 0;
      return;
    }
    // The Back input exits Wi-Fi selection and sync, then performs the cleanup
    // reboot. Until that happens, keep feeding the real activity input loop.
    if (!inputScript.empty()) {
      if (scriptIndex < inputScript.size()) runReaderInputScript();
      return;
    }
    if (fromLibrary) {
      auto* library = dynamic_cast<LibraryActivity*>(activityManager.simulatorCurrentActivity());
      if (!library || library->simulatorQuery() != "Sync" || library->simulatorSort() != 4 ||
          !library->simulatorDescending())
        fail("Sync did not restore the Library view");
      if (library->simulatorSelection() !=
              std::atoi(std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SELECTION")) ||
          library->simulatorTopIndex() != std::atoi(std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SCROLL"))) {
        LOG_ERR("SMOKE", "Library actual selection=%d scroll=%d expected=%s/%s rows=%d", library->simulatorSelection(),
                library->simulatorTopIndex(), std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SELECTION"),
                std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SCROLL"), library->simulatorRowCount());
        fail("Library selection or scroll changed after sync");
      }
      if (APP_STATE.pendingOverlayResume.valid()) fail("Library sync return route was not consumed");
      LOG_INF("SMOKE", "Simulator smoke test passed: Library menu sync returns through both reboots");
      std::_Exit(0);
    }
    auto* browser = dynamic_cast<FileBrowserActivity*>(activityManager.simulatorCurrentActivity());
    if (!browser || browser->simulatorFolderPath() != "/books") fail("Sync did not return to the original folder");
    if (browser->simulatorSelectedIndex() !=
            static_cast<size_t>(std::atoi(std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SELECTION"))) ||
        browser->simulatorTopIndex() != std::atoi(std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_SCROLL")))
      fail("Browser selection or scroll changed after sync");
    if (APP_STATE.pendingOverlayResume.valid()) fail("Browser sync return route was not consumed");
    LOG_INF("SMOKE", "Simulator smoke test passed: browser sync returns through both reboots");
    std::_Exit(0);
#else
    fail("Browser sync return smoke requires a touch device");
#endif
  }

  void tickFilenameFont() {
#if CROSSINK_SCALABLE_FONTS
    const char* family = std::getenv("CROSSINK_SIMULATOR_SMOKE_FILENAME_FONT");
    if (scriptIndex < inputScript.size()) {
      runReaderInputScript();
      return;
    }
    inputScript.clear();
    scriptIndex = 0;
    switch (filenameFontPhase++) {
      case 0: {
        RenderLock lock;
        SETTINGS.filenameFallbackFont[0] = '\0';
        SETTINGS.uiScale = CrossPointSettings::UI_SCALE_SMALL;
        SETTINGS.uiTheme = CrossPointSettings::LYRA;
        UITheme::getInstance().reload();
        filenameFontSystem.invalidate();
        if (!filenameFontSystem.ensureLoaded(renderer)) fail("None filename font failed");
        const auto device = buildSystemDeviceSettingsList(getSettingsList());
        if (device[3].action != SettingAction::Language || device[4].action != SettingAction::FilenameFallbackFont)
          fail("Filename setting is not directly below Language");
        std::vector<std::string> names;
        if (!filenameFontSystem.discover(names) || std::find(names.begin(), names.end(), family) == names.end())
          fail("Filename fixture family missing");
        for (const auto& name : names)
          if (name == "Variable Only" || name == "Bitmap Only" || name == "Corrupt Only")
            fail("Unsupported filename font offered");
        activityManager.replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInputManager));
        queueStep("Filename Settings entry", SmokeStep::Start, 4);
        break;
      }
      case 1:
        for (int i = 0; i < 3; ++i) addTap(MappedInputManager::Button::Confirm);
        addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Filename Device settings", 4));
        inputScript.push_back(assertSettingsNavigation(3, 1));
        for (int i = 0; i < 4; ++i) addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        inputScript.push_back(render("Filename setting below Language", 4));
        break;
      case 2: {
        RenderLock lock;
        captureStatusBarScreen("filename-device-setting");
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Filename font picker", 4));
        break;
      }
      case 3: {
        RenderLock lock;
        const auto* settings = dynamic_cast<SettingsActivity*>(activityManager.simulatorCurrentActivity());
        if (!settings || !settings->simulatorOptionPopupActive()) fail("Filename picker did not open");
        const auto& names = settings->simulatorFilenameFontNames();
        const auto found = std::find(names.begin(), names.end(), family);
        if (found == names.end()) fail("Filename picker lacks selected family");
        captureStatusBarScreen("filename-font-picker");
        for (int i = 0; i < std::distance(names.begin(), found); ++i) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Filename font applied", 5));
        break;
      }
      case 4: {
        RenderLock lock;
        if (std::strcmp(SETTINGS.filenameFallbackFont, family) != 0) fail("Filename selection was not applied");
        const auto& fonts = renderer.getFontMap();
        for (int id : {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID}) {
          const int compositeId = renderer.filenameFontId(id);
          if (compositeId == id) fail("Filename composite missing");
          const auto& primary = fonts.at(id);
          const auto& composite = fonts.at(compositeId);
          for (auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
            if (primary.getGlyphData('A', style).glyph != composite.getGlyphData('A', style).glyph)
              fail("Filename font replaced built-in Latin");
            const auto cjk = composite.getGlyphData(0x4e00, style);
            if (!composite.hasCodepoint(0x4e00) || !cjk.glyph || cjk.fontData == primary.getData(style))
              fail("Missing CJK fallback glyph");
          }
        }
        JsonDocument saved;
        SETTINGS.toJson(saved);
        saved["filenameFallbackFont"] = std::string(family);  // own bytes before mutating the settings buffer
        SETTINGS.filenameFallbackFont[0] = '\0';
        SETTINGS.fromJson(saved.as<JsonVariantConst>());
        if (std::strcmp(SETTINGS.filenameFallbackFont, family) != 0) fail("Filename JSON round trip failed");
        captureStatusBarScreen("filename-device-selected");
        Storage.mkdir("/books/日本語");
        if (!Storage.writeFile("/books/日本語/Latin 一丁.txt", "Filename font smoke"))
          fail("Cannot create mixed title");
        activityManager.replaceActivity(
            std::make_unique<FileBrowserActivity>(renderer, mappedInputManager, "/books/日本語"));
        queueStep("Filename mixed browser small", SmokeStep::Start, 8);
        break;
      }
      case 5: {
        RenderLock lock;
        captureStatusBarScreen("filename-browser-small");
        SETTINGS.uiScale = CrossPointSettings::UI_SCALE_LARGE;
        UITheme::getInstance().reload();
        activityManager.replaceActivity(
            std::make_unique<FileBrowserActivity>(renderer, mappedInputManager, "/books/日本語"));
        queueStep("Filename mixed browser large", SmokeStep::Start, 8);
        break;
      }
      case 6: {
        RenderLock lock;
        captureStatusBarScreen("filename-browser-large");
        RECENT_BOOKS.addOrUpdateBook("/books/日本語/Latin 一丁.txt", "Latin 一丁 日本語", "Author 日本語", "");
        SETTINGS.uiTheme = CrossPointSettings::LYRA;
        UITheme::getInstance().reload();
        activityManager.replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInputManager));
        queueStep("Filename mixed Home", SmokeStep::Start, 20);
        break;
      }
      case 7: {
        RenderLock lock;
        captureStatusBarScreen("filename-home");
        auto* home = dynamic_cast<HomeActivity*>(activityManager.simulatorCurrentActivity());
        if (!home) fail("Filename Home missing");
        home->onFrontlightPanelOpened();
        SETTINGS.filenameFallbackFont[0] = '\0';
        filenameFontSystem.ensureLoaded(renderer);
        home->onFrontlightPanelClosed();
        queueStep("Filename Home refreshed after None", SmokeStep::Start, 20);
        break;
      }
      case 8: {
        RenderLock lock;
        captureStatusBarScreen("filename-home-none");
        auto* home = dynamic_cast<HomeActivity*>(activityManager.simulatorCurrentActivity());
        if (!home || renderer.filenameFontId(UI_10_FONT_ID) != UI_10_FONT_ID) fail("Filename None Home mismatch");
        home->onFrontlightPanelOpened();
        std::strncpy(SETTINGS.filenameFallbackFont, family, sizeof(SETTINGS.filenameFallbackFont) - 1);
        if (!filenameFontSystem.ensureLoaded(renderer)) fail("Filename Home reenable failed");
        home->onFrontlightPanelClosed();
        queueStep("Filename Home refreshed after selection", SmokeStep::Start, 20);
        break;
      }
      case 9: {
        RenderLock lock;
        captureStatusBarScreen("filename-home-reenabled");
        SETTINGS.librarySortMethod = 4;  // Recently Read includes TXT fixtures
        SETTINGS.libraryUseMetadata = 1;
        SETTINGS.libraryShowTxt = 1;
        SETTINGS.libraryHideFinishedBooks = 0;
        activityManager.replaceActivity(std::make_unique<LibraryActivity>(renderer, mappedInputManager));
        queueStep("Filename mixed Library", SmokeStep::Start, 12);
        break;
      }
      case 10: {
        RenderLock lock;
        captureStatusBarScreen("filename-library");
        activityManager.simulatorCurrentActivity()->startActivityForResult(
            std::make_unique<StatsUploadActivity>(renderer, mappedInputManager), [](const ActivityResult&) {});
        queueStep("Filename network child releases font", SmokeStep::Start, 5);
        break;
      }
      case 11: {
        RenderLock lock;
        if (renderer.filenameFontId(UI_10_FONT_ID) != UI_10_FONT_ID) fail("Network child retained filename font");
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Filename Library restored after network child", 6));
        inputScript.push_back(assertActivity("Library"));
        break;
      }
      case 12: {
        RenderLock lock;
        if (renderer.filenameFontId(UI_10_FONT_ID) == UI_10_FONT_ID) fail("Network child return lost filename font");
        captureStatusBarScreen("filename-library-after-network");
        sdFontSystem.releaseForNetwork(renderer);
        if (renderer.filenameFontId(UI_10_FONT_ID) != UI_10_FONT_ID) fail("Filename font retained at storage release");
        if (!filenameFontSystem.ensureLoaded(renderer)) fail("Filename font failed after storage release");
        const auto identity = filenameFontSystem.fingerprint();
        sdFontSystem.releaseLoadedFont(renderer);
        if (renderer.filenameFontId(UI_10_FONT_ID) == UI_10_FONT_ID || filenameFontSystem.fingerprint() != identity)
          fail("Reader font release changed filename fallback");
        std::strcpy(SETTINGS.filenameFallbackFont, "Missing Family");
        if (filenameFontSystem.ensureLoaded(renderer) || renderer.filenameFontId(UI_10_FONT_ID) != UI_10_FONT_ID ||
            std::strcmp(SETTINGS.filenameFallbackFont, "Missing Family") != 0)
          fail("Missing family did not recover safely");
        std::strncpy(SETTINGS.filenameFallbackFont, family, sizeof(SETTINGS.filenameFallbackFont) - 1);
        if (!filenameFontSystem.ensureLoaded(renderer)) fail("Filename reload failed");
        SETTINGS.filenameFallbackFont[0] = '\0';
        if (!filenameFontSystem.ensureLoaded(renderer) || renderer.filenameFontId(UI_10_FONT_ID) != UI_10_FONT_ID)
          fail("None did not remove filename fallback");
        LOG_INF("SMOKE",
                "Simulator smoke test passed: filename picker, mixed Latin/CJK, both UI sizes, Library, Home refresh, "
                "persistence, storage "
                "release and reader independence");
        std::_Exit(0);
      }
    }
#else
    fail("Filename font smoke requires S3 scalable fonts");
#endif
  }

  void tickHyphenation() {
    if (scriptIndex < inputScript.size()) {
      runReaderInputScript();
      return;
    }
    inputScript.clear();
    scriptIndex = 0;
    const char* savedStage = std::getenv("CROSSINK_HYPHENATION_SMOKE_STAGE");
    const int stage = savedStage ? std::atoi(savedStage) : 0;
    auto* manager = dynamic_cast<HyphenationManagerActivity*>(activityManager.simulatorCurrentActivity());
    if (stage == 2) {
      switch (hyphenationPhase++) {
        case 0: {
          const char* ui = std::getenv("CROSSINK_HYPHENATION_SMOKE_UI");
          if (ui && std::strcmp(I18N.getCode(), ui)) fail("Pack removal changed the UI language");
          if (Hyphenator::patternIdentity("de-DE") || !Hyphenator::patternIdentity("en") ||
              !HyphenationPackStore::hasSource("de"))
            fail("Pack removal changed the wrong resources");
          SETTINGS.hyphenationEnabled = true;
          activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"), true);
          inputScript.push_back(render("Missing pack reader", 8));
          break;
        }
        case 1:
        case 4:
          if (!activityManager.isCurrentActivityNamed("Confirmation")) {
            --hyphenationPhase;
            break;  // Wait for the first laid-out page and its missing-pack prompt.
          }
          {
            RenderLock lock;
            captureStatusBarScreen("hyphenation-missing");
          }
          if (hyphenationPhase == 2) {
            addTap(MappedInputManager::Button::Back);
            inputScript.push_back(render("Read without missing pack", 12));
          } else {
            addTap(MappedInputManager::Button::Down);
            addTap(MappedInputManager::Button::Confirm);
            inputScript.push_back(render("Open manager from reader", 8));
          }
          break;
        case 2:
          if (!activityManager.isCurrentActivityNamed("EpubReader")) fail("Missing pack cancel did not resume reading");
          activityManager.goHome();
          inputScript.push_back(render("Home before new reader session", 4));
          break;
        case 3:
          activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"), true);
          inputScript.push_back(render("New reader session", 8));
          break;
        case 5:
          if (!manager) fail("Missing pack prompt did not open its manager");
          if (!manager->allowPowerAsConfirmInReaderMode()) fail("Reader manager disabled Power-as-Confirm");
          addTap(MappedInputManager::Button::Back);
          inputScript.push_back(render("Reader after manager cancel", 12));
          break;
        default:
          if (!activityManager.isCurrentActivityNamed("EpubReader")) fail("Manager cancel did not resume reading");
          LOG_INF("SMOKE",
                  "Simulator smoke test passed: hyphenation install/remove reboots, UI language preservation, "
                  "missing-pack prompt and reader return");
          std::_Exit(0);
      }
      return;
    }
    switch (hyphenationPhase++) {
      case 0: {
        const char* ui = std::getenv("CROSSINK_HYPHENATION_SMOKE_UI");
        if (ui && std::strcmp(I18N.getCode(), ui)) fail("Hyphenation update changed the UI language");
        if (HyphenationPackStore::isInstalled("de") != (stage == 1)) fail("Hyphenation boot selection mismatch");
        if (!HyphenationPackStore::hasSource("de")) fail("Hyphenation source was removed");
        if (!Hyphenator::patternIdentity("en")) fail("Built-in English unavailable");
        if (stage == 0 && HyphenationPackStore::install("fr") != hyphenation_pack::Result::Invalid)
          fail("A renamed pack bypassed language identity validation");
        if (stage == 1) {
          Hyphenator::setPreferredLanguage("GER");
          if (Hyphenator::breakOffsets("Satellitensystems", false).empty()) fail("Installed German patterns unused");
        }
        const auto settings = buildReaderPageLayoutSettingsList(getSettingsList());
        auto row = std::find_if(settings.begin(), settings.end(),
                                [](const auto& setting) { return setting.nameId == StrId::STR_HYPHENATION; });
        if (row == settings.end() || ++row == settings.end() || row->action != SettingAction::ManageHyphenation)
          fail("Hyphenation manager is not next to its setting");
        activityManager.replaceActivity(
            std::make_unique<HyphenationManagerActivity>(renderer, mappedInputManager, "de"));
        inputScript.push_back(render("Hyphenation language list", 5));
        break;
      }
      case 1:
        if (!manager || !manager->simulatorOptionDisabled(0)) fail("Built-in English is not protected");
        {
          RenderLock lock;
          captureStatusBarScreen(stage ? "hyphenation-installed" : "hyphenation-sd");
        }
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Hyphenation actions", 4));
        break;
      case 2:
        if (!manager || manager->simulatorOptionDisabled(1) || manager->simulatorOptionDisabled(2) != (stage == 0))
          fail("Hyphenation install/delete availability mismatch");
        {
          RenderLock lock;
          captureStatusBarScreen(stage ? "hyphenation-remove" : "hyphenation-install");
        }
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Cancel hyphenation change", 4));
        break;
      case 3:
        if (HyphenationPackStore::isInstalled("de") != (stage == 1)) fail("Cancel changed installed hyphenation");
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Reopen hyphenation actions", 4));
        break;
      case 4:
        setenv("CROSSINK_HYPHENATION_SMOKE_STAGE", stage ? "2" : "1", 1);
        addTap(MappedInputManager::Button::Down);
        if (stage == 1) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Apply hyphenation change", 10));
        break;
      default:
        fail("Hyphenation operation did not restart");
    }
  }

  void tickCalibration() {
    if (scriptIndex < inputScript.size()) {
      runReaderInputScript();
      return;
    }
    inputScript.clear();
    scriptIndex = 0;
    const auto renderCapture = [&](const char* name) {
      RenderLock lock;
      captureStatusBarScreen(name);
    };
    const auto tapControl = [&](ScreenCalibrationActivity& editor, int value) {
#if CROSSINK_APP_CAP_TOUCH
      freeink::ui::Interaction hit;
      bool found = false;
      for (int y = 48; y < renderer.getScreenHeight() - 48 && !found; y += 4) {
        for (int x = 48; x < renderer.getScreenWidth() - 48; x += 4) {
          if (editor.simulatorHit(x, y, hit) && hit.value == value) {
            found = true;
            break;
          }
        }
      }
      if (!found) fail("Calibration touch target not published: %d", value);
      inputScript.push_back(touchDown(hit.rect.x + hit.rect.width / 2, hit.rect.y + hit.rect.height / 2));
      inputScript.push_back(touchRelease(hit.rect.x + hit.rect.width / 2, hit.rect.y + hit.rect.height / 2));
#else
      (void)editor;
      (void)value;
#endif
    };
    const auto openTimePicker = [&] {
      {
        RenderLock lock;
        SETTINGS.uiTheme = calibrationPickerPass == 0 ? CrossPointSettings::CLASSIC : CrossPointSettings::ROUNDEDRAFF;
        SETTINGS.uiScale = CrossPointSettings::UI_SCALE_SMALL;
        UITheme::getInstance().reload();
        renderer.setOrientation(GfxRenderer::Portrait);
        renderer.setViewableInsets(ScreenInsets{{{32, 32, 32, 32}}});
      }
      calibrationPickerResult = -1;
      activityManager.simulatorCurrentActivity()->startActivityForResult(
          std::make_unique<FrontlightTimePickerActivity>(renderer, mappedInputManager, StrId::STR_FRONTLIGHT_SCHEDULE,
                                                         8 * 60 + 3),
          [this](const ActivityResult& result) {
            if (!result.isCancelled && std::holds_alternative<IntervalResult>(result.data))
              calibrationPickerResult = static_cast<int>(std::get<IntervalResult>(result.data).value);
          });
      queueStep("Calibration time picker entry", SmokeStep::Start, 4);
    };
    const auto tapTimePicker = [&](const int16_t value) {
      auto* picker = dynamic_cast<FrontlightTimePickerActivity*>(activityManager.simulatorCurrentActivity());
      RenderLock lock;
      freeink::ui::Interaction hit;
      if (!picker || !ScreenCalibrationSmokeTest::timePickerHit(*picker, value, hit))
        fail("Calibration time picker key %d or field bounds failed", value);
#if CROSSINK_APP_CAP_TOUCH
      inputScript.push_back(touchDown(hit.rect.x + hit.rect.width / 2, hit.rect.y + hit.rect.height / 2));
      inputScript.push_back(touchRelease(hit.rect.x + hit.rect.width / 2, hit.rect.y + hit.rect.height / 2));
#endif
      inputScript.push_back(render("Calibration time picker touch", 4));
    };
    switch (calibrationPhase++) {
      case 0: {
        {
          RenderLock lock;
          renderer.setOrientation(GfxRenderer::LandscapeClockwise);
        }
        activityManager.replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInputManager));
        queueStep("Calibration Settings entry", SmokeStep::Start, 4);
        break;
      }
      case 1: {
        const auto settings = buildGroupedDisplaySettingsList(getSettingsList());
        const auto it = std::find_if(settings.begin(), settings.end(), [](const auto& setting) {
          return setting.action == SettingAction::ScreenCalibration;
        });
        if (it == settings.end()) fail("Calibration missing from Display Settings");
        for (auto i = settings.begin(); i <= it; ++i)
          addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration opened from Settings", 4));
        inputScript.push_back(assertActivity("ScreenCalibration"));
        break;
      }
      case 2: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || renderer.getOrientation() != GfxRenderer::Portrait) fail("Calibration portrait entry failed");
        renderCapture("calibration-initial");
        calibrationExpected = renderer.getViewableInsets();
        addTap(MappedInputManager::Button::Confirm);
        for (int i = 0; i < 12; ++i) addTap(MappedInputManager::Button::Up);
        for (int i = 0; i < 20; ++i) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        for (int i = 0; i < 5; ++i) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        calibrationExpected.edges[0] = 20;
        calibrationExpected.adjust(1, 5);
        inputScript.push_back(render("Calibration button draft", 4));
        break;
      }
      case 3: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || editor->simulatorDraft() != calibrationExpected ||
            renderer.getViewableInsets() != ScreenInsets{})
          fail("Calibration draft changed live geometry");
        renderCapture("calibration-edited-buttons");
        for (int i = 0; i < 4; ++i) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration saved back to Settings", 4));
        inputScript.push_back(assertActivity("Settings"));
        break;
      }
      case 4:
        if (renderer.getViewableInsets() != calibrationExpected ||
            HalScreenCalibration::load() != calibrationExpected ||
            renderer.getOrientation() != GfxRenderer::LandscapeClockwise)
          fail("Calibration save or orientation restore failed");
        {
          const auto* settings = dynamic_cast<SettingsActivity*>(activityManager.simulatorCurrentActivity());
          if (!settings) fail("Calibration did not return to Settings");
          const auto safe = settings->simulatorSafeRect();
          const auto expected = calibrationExpected.rotated(static_cast<unsigned>(renderer.getOrientation()));
          if (safe.x != expected.edges[3] || safe.y != expected.edges[0] ||
              safe.width != renderer.getScreenWidth() - expected.edges[1] - expected.edges[3] ||
              safe.height != renderer.getScreenHeight() - expected.edges[0] - expected.edges[2])
            fail("Settings kept its old calibration safe area");
        }
        renderCapture("calibration-save-return");
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration reopened", 4));
        break;
      case 5: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || editor->simulatorDraft() != calibrationExpected)
          fail("Calibration reopen lost persisted values");
        for (int i = 0; i < 4; ++i) addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration reset draft", 4));
        break;
      }
      case 6: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || editor->simulatorDraft() != ScreenInsets{} ||
            renderer.getViewableInsets() != calibrationExpected || HalScreenCalibration::load() != calibrationExpected)
          fail("Calibration Reset was not transactional");
        renderCapture("calibration-reset-draft");
        addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Down);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration cancelled", 4));
        inputScript.push_back(assertActivity("Settings"));
        break;
      }
      case 7:
        if (renderer.getViewableInsets() != calibrationExpected ||
            HalScreenCalibration::load() != calibrationExpected ||
            renderer.getOrientation() != GfxRenderer::LandscapeClockwise)
          fail("Calibration Cancel changed saved geometry");
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration touch entry", 4));
        break;
      case 8: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor) fail("Calibration touch entry failed");
        if (mappedInputManager.hasTouch()) {
          tapControl(*editor, 12);  // Right edge increase.
          calibrationExpected.adjust(1, 1);
        } else {
          addTap(MappedInputManager::Button::Confirm);
          addTap(MappedInputManager::Button::Down);
          addTap(MappedInputManager::Button::Confirm);
          calibrationExpected.adjust(0, 1);
        }
        inputScript.push_back(render("Calibration touch draft", 4));
        break;
      }
      case 9: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || editor->simulatorDraft() != calibrationExpected) fail("Calibration touch adjustment failed");
        renderCapture("calibration-edited-touch");
        if (mappedInputManager.hasTouch())
          tapControl(*editor, 5);
        else {
          for (int i = 0; i < 5; ++i) addTap(MappedInputManager::Button::Down);
          addTap(MappedInputManager::Button::Confirm);
        }
        inputScript.push_back(render("Calibration touch saved", 4));
        break;
      }
      case 10: {
        if (!activityManager.isCurrentActivityNamed("Settings") ||
            renderer.getViewableInsets() != calibrationExpected || HalScreenCalibration::load() != calibrationExpected)
          fail("Calibration touch save failed");
        {
          RenderLock lock;
          SETTINGS.uiScale = CrossPointSettings::UI_SCALE_LARGE;
        }
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Calibration current language large UI", 4));
        break;
      }
      case 11: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor) fail("Calibration localized reopen failed");
        renderCapture("calibration-localized-large");
        const char* storage = std::getenv("CROSSINK_CALIBRATION_PATH");
        calibrationStoragePath = storage ? storage : ".screen-calibration.nvs";
        setenv("CROSSINK_CALIBRATION_PATH", "/missing-calibration-directory/settings", 1);
        if (mappedInputManager.hasTouch()) {
          tapControl(*editor, 11);
          inputScript.push_back(render("Calibration unsaved touch edit", 4));
          tapControl(*editor, 5);
        } else {
          addTap(MappedInputManager::Button::Confirm);
          addTap(MappedInputManager::Button::Down);
          addTap(MappedInputManager::Button::Confirm);
          for (int i = 0; i < 5; ++i) addTap(MappedInputManager::Button::Down);
          addTap(MappedInputManager::Button::Confirm);
        }
        inputScript.push_back(render("Calibration save error", 4));
        break;
      }
      case 12: {
        auto* editor = dynamic_cast<ScreenCalibrationActivity*>(activityManager.simulatorCurrentActivity());
        if (!editor || !editor->simulatorSaveFailed() || renderer.getViewableInsets() != calibrationExpected)
          fail("Calibration save error changed live geometry or dismissed the editor");
        renderCapture("calibration-save-failed");
        setenv("CROSSINK_CALIBRATION_PATH", calibrationStoragePath.c_str(), 1);
        if (HalScreenCalibration::load() != calibrationExpected) fail("Failed save changed persisted geometry");
        if (mappedInputManager.hasTouch())
          tapControl(*editor, 6);
        else
          addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Calibration save failure cancelled", 4));
        break;
      }
      case 13: {
        if (!activityManager.isCurrentActivityNamed("Settings"))
          fail("Calibration error cancel did not restore Settings");
        RenderLock lock;
        if (!ScreenCalibrationSmokeTest::geometry(
                lock, [this](const std::string& name) { captureStatusBarScreen(name.c_str()); }))
          fail("Calibration maximum headers, status, touch targets, or list bounds failed");
        if (!ScreenCalibrationSmokeTest::keyboardAndHints(
                lock, [this](const std::string& name) { captureStatusBarScreen(name.c_str()); }))
          fail("Calibration hints, keyboard edge keys, or font picker resume failed");
        for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                                       GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise}) {
          renderer.setOrientation(orientation);
          renderer.clearScreen();
          const auto safe = UITheme::getInstance().getScreenSafeArea(renderer);
          const auto expected = calibrationExpected.rotated(static_cast<unsigned>(orientation));
          if (safe.x != expected.edges[3] || safe.y != expected.edges[0] ||
              safe.width != renderer.getScreenWidth() - expected.edges[1] - expected.edges[3] ||
              safe.height != renderer.getScreenHeight() - expected.edges[0] - expected.edges[2])
            fail("Calibration safe area did not rotate");
          renderer.drawRect(safe.x, safe.y, safe.width, safe.height);
          GUI.drawHeader(renderer, Rect{safe.x, safe.y + 40, safe.width, 100}, tr(STR_SCREEN_CALIBRATION));
          captureStatusBarScreen(("calibration-orientation-" + std::to_string(static_cast<int>(orientation))).c_str());
        }
        renderer.setOrientation(GfxRenderer::Portrait);
        if (!ScreenCalibrationSmokeTest::txt(lock))
          fail("TXT calibration reflow or reading-position preservation failed");
        captureStatusBarScreen("calibration-txt-reflow");
        lock.unlock();
        activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"));
        queueStep("Calibration EPUB entry", SmokeStep::Start, 5);
        break;
      }
      case 14: {
        auto* reader = dynamic_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
        if (!reader || !ScreenCalibrationSmokeTest::epubReady(*reader)) {
          --calibrationPhase;
          queueStep("Calibration EPUB ready", SmokeStep::Start, 4);
          break;
        }
        renderCapture("calibration-epub-before-reflow");
        if (!ScreenCalibrationSmokeTest::reflowEpub(*reader))
          fail("EPUB did not invalidate changed calibration dimensions");
        queueStep("Calibration EPUB reflow", SmokeStep::Start, 5);
        break;
      }
      case 15: {
        auto* reader = dynamic_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
        if (!reader || !ScreenCalibrationSmokeTest::epubReady(*reader)) {
          --calibrationPhase;
          queueStep("Calibration EPUB reflow complete", SmokeStep::Start, 4);
          break;
        }
        RenderLock lock;
        if (!ScreenCalibrationSmokeTest::reflowedEpub(*reader))
          fail("EPUB calibration reflow lost position or viewport");
        captureStatusBarScreen("calibration-epub-after-reflow");
        lock.unlock();
        activityManager.goHome();
        queueStep("Calibration Home resume entry", SmokeStep::Start, 4);
        break;
      }
      case 16: {
        auto* home = dynamic_cast<HomeActivity*>(activityManager.simulatorCurrentActivity());
        if (!home) fail("Calibration Home entry failed");
        calibrationResumedHome = home;
        home->onFrontlightPanelOpened();
        {
          RenderLock lock;
          renderer.setViewableInsets(ScreenInsets{{{32, 32, 32, 32}}});
        }
        home->onFrontlightPanelClosed();
        queueStep("Calibration Home geometry rebuilt", SmokeStep::Start, 4);
        break;
      }
      case 17: {
        if (!activityManager.isCurrentActivityNamed("Home") ||
            activityManager.simulatorCurrentActivity() == calibrationResumedHome)
          fail("Home did not recreate layout after calibration returned from panel");
        renderCapture("calibration-home-resume-max32");
        activityManager.goToFileBrowser("/books");
        queueStep("Calibration browser resume entry", SmokeStep::Start, 4);
        break;
      }
      case 18: {
        auto* browser = dynamic_cast<FileBrowserActivity*>(activityManager.simulatorCurrentActivity());
        if (!browser || browser->simulatorSafeRect().x != 32) fail("Browser initial calibration bounds failed");
        {
          RenderLock lock;
          renderer.setViewableInsets(ScreenInsets{});
        }
        browser->onFrontlightPanelClosed();
        queueStep("Calibration browser defaults restored", SmokeStep::Start, 4);
        break;
      }
      case 19: {
        auto* browser = dynamic_cast<FileBrowserActivity*>(activityManager.simulatorCurrentActivity());
        if (!browser || browser->simulatorSafeRect().x != 3 || browser->simulatorSafeRect().y != 9)
          fail("Browser retained old SDK bounds after calibration reset");
        renderCapture("calibration-browser-resume-reset");
        if (mappedInputManager.hasTouchHardware()) {
          openTimePicker();
          break;
        }
        LOG_INF("SMOKE",
                "Simulator smoke test passed: calibration buttons/touch, persistence/errors, localization, rotations, "
                "headers/list bounds, suspended Home/browser/menu, TXT/EPUB reflow");
        std::_Exit(0);
      }
      case 20:
        renderCapture(calibrationPickerPass == 0 ? "calibration-time-picker-classic-max32"
                                                 : "calibration-time-picker-roundedraff-max32");
        tapTimePicker(1);
        break;
      case 21:
        // The pending first digit enables the adjacent backspace target.
        {
          auto* picker = dynamic_cast<FrontlightTimePickerActivity*>(activityManager.simulatorCurrentActivity());
          RenderLock lock;
          freeink::ui::Interaction hit;
          if (!picker || !ScreenCalibrationSmokeTest::timePickerHit(*picker, -1, hit))
            fail("Calibration time picker backspace bounds failed");
        }
        tapTimePicker(-1);
        break;
      case 22:
        tapTimePicker(0);
        break;
      case 23: {
        const auto* picker = dynamic_cast<FrontlightTimePickerActivity*>(activityManager.simulatorCurrentActivity());
        if (!picker || !ScreenCalibrationSmokeTest::timePickerHourIsTwelve(*picker))
          fail("Calibration time picker 0 did not select 12 AM");
        tapTimePicker(-2);
        break;
      }
      case 24:
        if (!activityManager.isCurrentActivityNamed("FileBrowser") || calibrationPickerResult != 3)
          fail("Calibration time picker OK did not return 12:03 AM");
        if (++calibrationPickerPass < 2) {
          calibrationPhase = 20;
          openTimePicker();
          break;
        }
        LOG_INF("SMOKE", "Simulator smoke test passed: calibration and max32 Classic/RoundedRaff time picker touch");
        std::_Exit(0);
    }
  }

  void tickAbout() {
    if (scriptIndex < inputScript.size()) {
      runReaderInputScript();
      return;
    }
    inputScript.clear();
    scriptIndex = 0;
    switch (aboutPhase++) {
      case 0: {
        // Repeat the actual Settings entry/return route at both scales and rotations.
        {
          RenderLock lock;
          SETTINGS.uiScale = aboutPass % 2 ? CrossPointSettings::UI_SCALE_LARGE : CrossPointSettings::UI_SCALE_SMALL;
          SETTINGS.uiTheme = aboutPass < 2 ? CrossPointSettings::LYRA : CrossPointSettings::CLASSIC;
          static constexpr GfxRenderer::Orientation orientations[] = {
              GfxRenderer::Orientation::Portrait, GfxRenderer::Orientation::LandscapeClockwise,
              GfxRenderer::Orientation::PortraitInverted, GfxRenderer::Orientation::LandscapeCounterClockwise};
          renderer.setOrientation(orientations[aboutPass / 2]);
          UITheme::getInstance().reload();
        }
        activityManager.replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInputManager));
        queueStep("About Settings entry", SmokeStep::Start, 4);
        break;
      }
      case 1: {
        const auto settings = buildSystemSettingsParentList(getSettingsList());
        if (settings.back().action != SettingAction::About) fail("About missing from System");
        for (int i = 0; i < 3; ++i) addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("About System tab", 3));
        for (size_t i = 0; i < settings.size(); ++i)
          addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        inputScript.push_back(assertSettingsNavigation(3, static_cast<int>(settings.size())));
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("About opened from Settings", 4));
        inputScript.push_back(assertActivity("About"));
        break;
      }
      case 2: {
        auto* about = dynamic_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (!about || about->simulatorTopIndex() != 0) fail("About initial viewport mismatch");
        if (aboutPass % 2 && !about->simulatorFirstHeading()) {
          RenderLock lock;
          about->simulatorSetFirstHeading(
              "Geräteprofil mit ausführlicher Hardwarebeschreibung und Diagnoseinformationen");
          aboutPhase = 2;
          inputScript.push_back(render("About long localized heading", 3));
          break;
        }
#if CROSSINK_APP_CAP_TOUCH
        const auto action = about->simulatorExportButtonRect();
        const auto header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
        if (action.width <= 0 || action.height < 56 || action.x < UITheme::getInstance().getMetrics().listSidePadding ||
            action.y < header.y + header.height ||
            action.x + action.width > renderer.getScreenWidth() - UITheme::getInstance().getMetrics().listSidePadding ||
            action.y + action.height >= renderer.getScreenHeight())
          fail("About full-width export action escaped content bounds");
        {
          RenderLock lock;
          if (!renderer.isPixelBlack(action.x, action.y + action.height / 2) ||
              !renderer.isPixelBlack(action.x + action.width / 2, action.y))
            fail("About export action has no visible outline");
        }
#else
        if (about->simulatorExportButtonRect().width != 0) fail("About export action shown on button device");
#endif
        const auto& snapshot = about->simulatorSnapshot();
        if (!snapshot.simulated || !snapshot.device || snapshot.width != display.getDisplayWidth() ||
            snapshot.height != display.getDisplayHeight() || snapshot.chip || snapshot.sdk || snapshot.internalFree)
          fail("About simulator presented real hardware data");
        if ((snapshot.touch == HalDeviceInfo::Presence::Simulated) != gpio.hasTouch())
          fail("About touch profile mismatch");
        aboutSnapshotUptime = snapshot.uptimeSeconds;
        {
          RenderLock lock;
          captureStatusBarScreen(("about-first-" + std::to_string(aboutPass)).c_str());
        }
        const int nextPage =
            std::min(about->simulatorVisibleRows(), about->simulatorRowCount() - about->simulatorVisibleRows());
        // Even a slow release must page once, with no auto-repeat before release.
        for (const auto button : {MappedInputManager::Button::Down, MappedInputManager::Button::Up}) {
          const bool down = button == MappedInputManager::Button::Down;
          inputScript.push_back(press(button));
          inputScript.push_back({ScriptActionType::WaitForNavigationHold, button, nullptr, 0, 0, 0});
          inputScript.push_back({ScriptActionType::AssertAboutTopIndex, button, nullptr, 0, down ? 0 : nextPage, 0});
          inputScript.push_back(release(button));
          inputScript.push_back(render("About single page after held-button release", 3));
          inputScript.push_back({ScriptActionType::AssertAboutTopIndex, button, nullptr, 0, down ? nextPage : 0, 0});
        }
        addTap(MappedInputManager::Button::Down);
        inputScript.push_back(render("About next page", 3));
        break;
      }
      case 3: {
        const auto* about = dynamic_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (!about ||
            about->simulatorTopIndex() !=
                std::min(about->simulatorVisibleRows(), about->simulatorRowCount() - about->simulatorVisibleRows()))
          fail("About Down skipped or repeated a page");
        // Repeated paging must reach the last diagnostic and clamp at the end.
        for (int i = 0; i < 30; ++i) addTap(MappedInputManager::Button::Right);
        inputScript.push_back(render("About last page", 3));
        inputScript.push_back(assertActivity("About"));
        break;
      }
      case 4: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (about->simulatorTopIndex() + about->simulatorVisibleRows() != about->simulatorRowCount() ||
            about->simulatorSnapshot().uptimeSeconds != aboutSnapshotUptime)
          fail("About last page or stable snapshot mismatch");
        {
          RenderLock lock;
          captureStatusBarScreen(("about-last-" + std::to_string(aboutPass)).c_str());
        }
#if CROSSINK_APP_CAP_TOUCH
        const int x = renderer.getScreenWidth() / 2;
        const int y = renderer.getScreenHeight() / 2;
        inputScript = {touchDown(x, y), touchMove(x, y + 120), touchRelease(x, y + 120),
                       render("About swipe previous", 3)};
#else
        addTap(MappedInputManager::Button::Up);
        inputScript.push_back(render("About previous page", 3));
#endif
        break;
      }
      case 5: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (about->simulatorTopIndex() != std::max(0, about->simulatorRowCount() - about->simulatorVisibleRows() * 2))
          fail("About Up skipped or repeated a page");
#if CROSSINK_APP_CAP_TOUCH
        // The edge of the full-width outline belongs to the action too.
        const auto hit = about->simulatorExportButtonRect();
        // Wait for the activity's own repaint; a forced render masks broken feedback.
        inputScript = {touchDown(hit.x + 1, hit.y + hit.height / 2), render(nullptr, 6)};
#else
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("About export scope via physical Confirm", 3));
#endif
        break;
      }
      case 6: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
#if CROSSINK_APP_CAP_TOUCH
        if (!about->simulatorExportPressed() || about->simulatorScopePopupActive())
          fail("About export press feedback or release timing mismatch");
        {
          RenderLock lock;
          const auto hit = about->simulatorExportButtonRect();
          if (!renderer.isPixelBlack(hit.x + 3, hit.y + 3)) fail("About export press did not invert the action");
          captureStatusBarScreen(("about-pressed-" + std::to_string(aboutPass)).c_str());
        }
        const auto hit = about->simulatorExportButtonRect();
        inputScript = {touchRelease(hit.x + 1, hit.y + hit.height / 2), render("About export edge release", 3)};
#else
        if (!about->simulatorScopePopupActive()) fail("About Confirm did not open export scope");
#endif
        break;
      }
      case 7: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (!about->simulatorScopePopupActive()) fail("About export full-width hitbox missed its edge");
        {
          RenderLock lock;
          captureStatusBarScreen(("about-scope-" + std::to_string(aboutPass)).c_str());
        }
        // Cancel is still selected by default on scope entry.
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("About default scope cancellation", 3));
        break;
      }
      case 8: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (about->simulatorScopePopupActive() || Storage.exists(SupportInfo::Path))
          fail("About scope cancellation wrote export or stayed open");
#if CROSSINK_APP_CAP_TOUCH
        const auto hit = about->simulatorExportButtonRect();
        // Horizontal drag-off does not page the list, so only the activity's
        // press-state repaint can clear the inversion after cancellation.
        inputScript = {touchDown(hit.x + hit.width / 2, hit.y + hit.height / 2),
                       touchMove(hit.x - 1, hit.y + hit.height / 2), touchRelease(hit.x - 1, hit.y + hit.height / 2),
                       render(nullptr, 6)};
#endif
        break;
      }
      case 9: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (about->simulatorScopePopupActive() || about->simulatorExportPressed())
          fail("About drag-off activated export or left pressed feedback");
#if CROSSINK_APP_CAP_TOUCH
        const auto hit = about->simulatorExportButtonRect();
        {
          RenderLock lock;
          if (renderer.isPixelBlack(hit.x + 3, hit.y + 3)) fail("About drag-off did not repaint cleared feedback");
        }
        inputScript = {touchDown(hit.x + hit.width / 2, hit.y + hit.height + 2),
                       touchRelease(hit.x + hit.width / 2, hit.y + hit.height + 2),
                       render("About tap outside export", 3)};
#endif
        break;
      }
      case 10: {
        const auto* about = static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (about->simulatorScopePopupActive()) fail("About export hitbox extends below its outline");
        supportOpenScope();
        break;
      }
      case 11:
        supportSelectScope(1);
        break;
      case 12: {
        RenderLock lock;
        captureStatusBarScreen(("about-confirmation-" + std::to_string(aboutPass)).c_str());
        // Confirm without moving the selection must choose the default Cancel.
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("About default confirmation cancellation", 4));
        inputScript.push_back(assertActivity("About"));
        break;
      }
      case 13: {
        if (Storage.exists(SupportInfo::Path)) fail("About confirmation cancellation wrote export");
#if CROSSINK_APP_CAP_TOUCH
        const auto header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
        const auto hit = TouchHeaderBackButton::layout(header).touchRect;
        inputScript = {touchDown(hit.x + hit.width / 2, hit.y + hit.height / 2),
                       touchRelease(hit.x + hit.width / 2, hit.y + hit.height / 2)};
#else
        addTap(MappedInputManager::Button::Back);
#endif
        inputScript.push_back(render("About returns to Settings", 4));
        inputScript.push_back(
            assertSettingsNavigation(3, static_cast<int>(buildSystemSettingsParentList(getSettingsList()).size())));
        break;
      }
      case 14:
        if (++aboutPass < 8) {
          aboutPhase = 0;
          break;
        }
        LOG_INF("SMOKE",
                "Simulator smoke test passed: About navigation, paging, snapshot, bold/long headings, full-width "
                "export outline/press/hitbox, default cancellations, scales and orientations");
        std::_Exit(0);
    }
  }

  void supportOpenScope() {
    auto* about = dynamic_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
    if (!about) fail("Support export did not return to About");
#if CROSSINK_APP_CAP_TOUCH
    const auto hit = about->simulatorExportButtonRect();
    inputScript = {touchDown(hit.x + hit.width / 2, hit.y + hit.height / 2),
                   touchRelease(hit.x + hit.width / 2, hit.y + hit.height / 2)};
#else
    addTap(MappedInputManager::Button::Confirm);
#endif
    inputScript.push_back(render("Support scope selection", 3));
  }
  void supportSelectScope(int scope) {
    auto* about = dynamic_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
    if (!about || !about->simulatorScopePopupActive()) fail("Support scope popup missing");
#if CROSSINK_APP_CAP_TOUCH
    const auto hit = about->simulatorScopePopup().simulatorOptionRect(scope);
    inputScript = {touchDown(hit.x + hit.width / 2, hit.y + hit.height / 2),
                   touchRelease(hit.x + hit.width / 2, hit.y + hit.height / 2)};
#else
    for (int i = 0; i < scope; ++i) addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
    addTap(MappedInputManager::Button::Confirm);
#endif
    inputScript.push_back(render("Support scope confirmation", 4));
    inputScript.push_back(assertActivity("Confirmation"));
  }
  void supportConfirmWrite() {
    addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Support export result", 4));
    inputScript.push_back(assertActivity("About"));
  }
  void checkSupportExport(bool book) {
    const std::string contents = Storage.readFile(SupportInfo::Path).c_str();
    if (contents.find("PRIVATE_SUPPORT") != std::string::npos) fail("Support export leaked private data");
    JsonDocument doc;  // Host-only regression inspection, never part of firmware export.
    if (deserializeJson(doc, contents)) fail("Support export invalid JSON");
    if (doc["version"].as<int>() != 1 || !doc["about"]["runtimeData"].is<const char*>())
      fail("Support export schema missing");
    if (std::strcmp(doc["about"]["chipAndMemoryStatus"], "unsupported") || !doc["about"]["internalFreeBytes"].isNull())
      fail("Support export invented simulator heap");
    if (std::strcmp(doc["firmware"]["freeinkSdkSha"], AppVersion::sdkSha()))
      fail("Support export SDK provenance mismatch");
    if (!book) {
      if (std::strcmp(doc["bookContext"]["status"], "excluded") ||
          !doc["bookContext"]["effectiveReaderPreferences"].isNull())
        fail("Device scope included book settings");
    } else {
      const auto prefs = doc["bookContext"]["effectiveReaderPreferences"];
      if (std::strcmp(doc["bookContext"]["status"], "loaded") ||
          prefs["readerFontPointSize"]["value"].as<int>() != 27 ||
          std::strcmp(prefs["readerFontPointSize"]["source"], "book_override") ||
          std::strcmp(prefs["orientation"]["source"], "global_default") ||
          !doc["bookContext"]["fontSelectionOverride"].as<bool>() ||
          !doc["bookContext"]["dictionaryOverride"].as<bool>())
        fail("Support book inheritance mismatch");
    }
  }
  void checkSupportLegacyAndMissingRecords() {
    const auto cache = Epub::cachePathForFilePath(APP_STATE.openEpubPath, "/.crosspoint");
    const auto path = cache + "/reader_settings.bin";
    uint8_t legacy[88]{};  // Actual v4 format: single margin, legacy dark byte, no dictionary or mask.
    legacy[0] = 4;
    legacy[1] = 1;
    legacy[6] = 2;
    legacy[8] = 255;
    legacy[9] = 99;
    legacy[10] = 255;
    legacy[12] = 99;
    legacy[13] = 2;
    legacy[17] = 99;
    auto writeRecord = [&] {
      auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
      if (!file || file.write(legacy, sizeof(legacy)) != sizeof(legacy) || !file.sync())
        fail("Legacy support fixture write failed");
      file.close();
    };
    auto exportDocument = [&] {
      if (SupportInfoExport::save(true) != SupportInfo::Result::Saved) fail("Support context export failed");
      JsonDocument document;
      const auto contents = Storage.readFile(SupportInfo::Path);
      if (deserializeJson(document, contents)) fail("Support context invalid JSON");
      if (std::strstr(contents.c_str(), "PRIVATE_SUPPORT")) fail("Support context leaked private data");
      return document;
    };
    // A v11 image override must not be mistaken for a custom-font override.
    uint8_t grayscale[158]{};
    grayscale[0] = 11;
    grayscale[1] = 1;
    grayscale[155] = 4;  // Image grayscale is bit 18; custom font moved to bit 19.
    grayscale[157] = SETTINGS.imageGrayscale ? 0 : 1;
    {
      auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
      if (!file || file.write(grayscale, sizeof(grayscale)) != sizeof(grayscale) || !file.sync())
        fail("Grayscale support fixture write failed");
      file.close();
      auto d = exportDocument();
      auto field = d["bookContext"]["effectiveReaderPreferences"]["imageGrayscale"];
      if (!field["value"].is<int>() || field["value"].as<int>() != grayscale[157] ||
          std::strcmp(field["source"], "book_override") || d["bookContext"]["fontSelectionOverride"].as<bool>())
        fail("Support grayscale override confused with custom font");
    }
    writeRecord();
    {
      auto d = exportDocument();
      auto prefs = d["bookContext"]["effectiveReaderPreferences"];
      if (prefs["readerFontPointSize"]["value"].as<int>() != 14 ||
          prefs["lineHeightPercent"]["value"].as<int>() != 70 || prefs["wordSpacing"]["value"].as<int>() != 4 ||
          prefs["screenMarginHorizontal"]["value"].as<int>() != 150 ||
          prefs["orientation"]["value"].as<int>() != SETTINGS.orientation ||
          prefs["imageRendering"]["value"].as<int>() != SETTINGS.imageRendering ||
          prefs["embeddedStyle"]["value"].as<int>() != 1)
        fail("Legacy support normalization mismatch");
    }
    std::memcpy(legacy + 24, "PRIVATE_SUPPORT_SENTINEL_FONT", 29);
    writeRecord();
    {
      auto d = exportDocument();
      auto field = d["bookContext"]["effectiveReaderPreferences"]["readerFontPointSize"];
      if (!field["value"].isNull() || std::strcmp(field["status"], "unavailable_legacy_custom_font_size"))
        fail("Legacy custom font size misrepresented");
    }
    legacy[0] = 99;
    writeRecord();
    {
      auto d = exportDocument();
      if (std::strcmp(d["bookContext"]["status"], "invalid_record") ||
          !d["bookContext"]["effectiveReaderPreferences"].isNull())
        fail("Invalid support context misrepresented");
    }
    if (!Storage.remove(path.c_str())) fail("Support missing-record fixture failed");
    {
      auto d = exportDocument();
      if (std::strcmp(d["bookContext"]["status"], "inherited_no_record") ||
          std::strcmp(d["bookContext"]["effectiveReaderPreferences"]["readerFontPointSize"]["source"],
                      "global_default"))
        fail("Missing support record did not inherit");
    }
    if (!Storage.rmdir(cache.c_str())) fail("Support unavailable-cache fixture failed");
    {
      auto d = exportDocument();
      if (std::strcmp(d["bookContext"]["status"], "unavailable")) fail("Absent support cache misrepresented");
    }
  }

  void tickSupportExport() {
    if (scriptIndex < inputScript.size()) {
      runReaderInputScript();
      return;
    }
    inputScript.clear();
    scriptIndex = 0;
    switch (supportPhase++) {
      case 0: {
        {
          RenderLock lock;
          std::strcpy(SETTINGS.deviceName, "PRIVATE_SUPPORT_SENT");
          std::strcpy(SETTINGS.opdsServerUrl, "PRIVATE_SUPPORT_SENTINEL_URL");
          std::strcpy(SETTINGS.opdsUsername, "PRIVATE_SUPPORT_SENTINEL_USER");
          std::strcpy(SETTINGS.opdsPassword, "PRIVATE_SUPPORT_SENTINEL_PASSWORD");
          std::strcpy(SETTINGS.sdFontFamilyName, "PRIVATE_SUPPORT_SENTINEL_FONT");
          std::strcpy(SETTINGS.dictionarySdFontFamilyName, "PRIVATE_SUPPORT_SENTINEL_DICT");
          std::strcpy(SETTINGS.opdsDownloadFolder, "PRIVATE_SUPPORT_SENTINEL_FOLDER");
          std::strcpy(SETTINGS.nearbyReceiveFolder, "PRIVATE_SUPPORT_SENTINEL_NEARBY");
        }
        APP_STATE.openEpubPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        const auto cache = Epub::cachePathForFilePath(APP_STATE.openEpubPath, "/.crosspoint");
        Storage.mkdir(cache.c_str());
        uint8_t record[157]{};  // Fixed v10 fixture; includes poisoned font names, no metadata parse.
        record[0] = 10;
        record[1] = 9;
        record[6] = 27;
        record[7] = 100;
        record[10] = 5;
        record[11] = 5;
        std::memcpy(record + 24, "PRIVATE_SUPPORT_SENTINEL_BOOK_FONT", 33);
        std::memcpy(record + 88, "PRIVATE_SUPPORT_SENTINEL_BOOK_DICT", 33);
        record[152] = 18;
        record[153] = 2;
        record[155] = 4;  // font point-size bit1, font name bit18.
        auto f = Storage.open((cache + "/reader_settings.bin").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (!f || f.write(record, sizeof(record)) != sizeof(record) || !f.sync()) fail("Support fixture write failed");
        f.close();
        for (const char* name : {"wifi.json", "opds.json", "koreader.json", "ttf-rendering.json"})
          Storage.writeFile((std::string("/.crosspoint/") + name).c_str(), "PRIVATE_SUPPORT_SENTINEL_CONFIG");
        activityManager.replaceActivity(std::make_unique<AboutActivity>(renderer, mappedInputManager));
        queueStep("Support About entry", SmokeStep::Start, 4);
        break;
      }
      case 1:
        supportOpenScope();
        break;
      case 2:  // Default selection cancels; it must never generate a file.
      {
        RenderLock lock;
        captureStatusBarScreen("support-scope");
      }
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("Support scope cancelled", 3));
        break;
      case 3:
        if (Storage.exists(SupportInfo::Path)) fail("Scope cancellation wrote export");
        supportOpenScope();
        break;
      case 4:
        supportSelectScope(1);
        break;
      case 5: {
        RenderLock lock;
        captureStatusBarScreen("support-confirmation");
      }
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Support confirmation cancelled", 4));
        inputScript.push_back(assertActivity("About"));
        break;
      case 6:
        if (Storage.exists(SupportInfo::Path)) fail("Confirmation cancellation wrote export");
        supportOpenScope();
        break;
      case 7:
        supportSelectScope(1);
        break;
      case 8:
        supportConfirmWrite();
        break;
      case 9: {
        RenderLock lock;
        captureStatusBarScreen("support-saved");
      }
        checkSupportExport(false);
        priorSupportExport = Storage.readFile(SupportInfo::Path).c_str();
        // A directory at the temp filename deterministically rejects the HAL write.
        if (!Storage.mkdir(SupportInfo::TempPath) || !Storage.writeFile("/crossink-support.json.tmp/blocker", "test"))
          fail("Support failure fixture failed");
        supportOpenScope();
        break;
      case 10:
        supportSelectScope(1);
        break;
      case 11:
        supportConfirmWrite();
        break;
      case 12: {
        RenderLock lock;
        captureStatusBarScreen("support-failed");
      }
        if (std::string(Storage.readFile(SupportInfo::Path).c_str()) != priorSupportExport)
          fail("Failed export destroyed prior file");
        if (!Storage.remove("/crossink-support.json.tmp/blocker") || !Storage.rmdir(SupportInfo::TempPath))
          fail("Support failure fixture cleanup failed");
        if (static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity())->simulatorExportStatus() !=
            StrId::STR_SUPPORT_FAILED)
          fail("Support failure was not shown");
        supportOpenScope();
        break;
      case 13:
        supportSelectScope(2);
        break;
      case 14:
        supportConfirmWrite();
        break;
      case 15:
        checkSupportExport(true);
        checkSupportLegacyAndMissingRecords();
        if (Storage.exists(SupportInfo::TempPath) || Storage.exists(SupportInfo::BackupPath))
          fail("Support transaction files remained");
        if (static_cast<AboutActivity*>(activityManager.simulatorCurrentActivity())->simulatorExportStatus() !=
            StrId::STR_SUPPORT_SAVED)
          fail("Support success was not shown");
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Support About exit", 4));
        break;
      case 16:
        LOG_INF("SMOKE",
                "Simulator smoke test passed: support export privacy, explicit scopes, cancellation, failure "
                "preservation and EPUB inheritance");
        std::_Exit(0);
    }
  }

  void tickImpl() {
    mappedInputManager.simulatorClearInputFrame();

    if (settleFrames > 0) {
      --settleFrames;
      if (settleFrames == 0 && activeStepName != nullptr) {
        renderCurrentStep(activeStepName);
        activeStepName = nullptr;
      }
      return;
    }

    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_HYPHENATION")) {
      tickHyphenation();
      return;
    }
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FILENAME_FONT")) {
      tickFilenameFont();
      return;
    }
    if (tickOpdsCatalogSmokeTest()) return;

    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SUPPORT_EXPORT")) {
      tickSupportExport();
      return;
    }
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_CALIBRATION")) {
      tickCalibration();
      return;
    }
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_ABOUT")) {
      tickAbout();
      return;
    }
    if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_SYNC_RETURN")) {
      tickFileBrowserSyncReturn();
      return;
    }

    switch (step) {
      case SmokeStep::Start:
        LOG_INF("SMOKE", "Starting simulator smoke test");
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_APP_NOTES")) {
          activityManager.replaceActivity(std::make_unique<EntryRenderSmokeActivity>(renderer, mappedInputManager));
          queueStep("Entry render isolation and short note", SmokeStep::Done, 8);
          break;
        }
        if (std::getenv("CROSSINK_READING_TEST_MENU") && Storage.exists("/expected-progress.json")) {
          if (Storage.exists("/menu-network-checked")) {
            if (!activityManager.isCurrentActivityNamed("Library") || APP_STATE.pendingOverlayResume.valid())
              fail("Completed menu sync did not return to Library");
            LOG_INF("SMOKE", "Stats upload transport smoke passed");
            std::_Exit(0);
          }
          if (!Storage.writeFile("/menu-network-checked", "checked")) fail("Cannot mark menu sync reboot");
          const char* selectedBook =
              std::getenv("CROSSINK_READING_TEST_COLD") ? "/read/unread.epub" : "/read/first.epub";
          if (APP_STATE.openEpubPath != selectedBook || !activityManager.hasActivityNamed(KOReaderSyncActivity::NAME))
            fail("Book menu did not reboot into selected book sync");
          inputScript.clear();
          scriptIndex = 0;
          inputCompletionStep = SmokeStep::StatsUploadEmptyDone;
          inputScript.push_back(render("Selected book combined sync", 100));
          step = SmokeStep::ReaderInput;
          break;
        }
        // Applying remote progress intentionally reboots into the reader. The
        // isolated fixture marks re-entry so this test cannot repeat uploads.
        if (std::getenv("CROSSINK_READING_TEST_CURRENT") && Storage.exists("/expected-progress.json")) {
          LOG_INF("SMOKE", "Stats upload transport smoke passed");
          std::_Exit(0);
        }
        verifyStatsUploadContract();
        if (prepareReadingUploadSmokeTest()) {
          WiFi.mode(WIFI_STA);
          WiFi.begin("reading-smoke");
          if (std::getenv("CROSSINK_READING_TEST_MENU")) {
            for (const auto* path : {"/read/first.epub", "/read/notes.txt", "/read/book.xtc"}) {
              const auto actions = BookActions::buildBookActionItems(path, true);
              const auto count = std::count_if(actions.begin(), actions.end(), [](const auto& item) {
                return item.action == FileBrowserAction::SyncProgress;
              });
              if (count != (std::string(path) == "/read/notes.txt" ? 0 : 1))
                fail("Book menu sync availability mismatch");
            }
            if (!WIFI_STORE.addCredential("reading-smoke", "")) fail("Cannot save test Wi-Fi");
            WIFI_STORE.setLastConnectedSsid("reading-smoke");
            APP_STATE.openEpubPath = "/read/sub/second.EPUB";
            if (!APP_STATE.saveToFile() || !KOREADER_STORE.saveToFile()) fail("Cannot save menu sync fixture");
            PendingOverlayResume resume;
            resume.origin = PendingOverlayOrigin::Library;
            resume.tab = 4;
            resume.pane = true;
            BookActions::syncProgress(
                renderer, mappedInputManager,
                std::getenv("CROSSINK_READING_TEST_COLD") ? "/read/unread.epub" : "/read/first.epub",
                std::move(resume));
            break;
          }
          const bool currentBook = std::getenv("CROSSINK_READING_TEST_CURRENT");
          if (currentBook)
            activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
                renderer, mappedInputManager, "/read/first.epub", DocumentMatchMethod::FILENAME, SETTINGS.orientation));
          else if (std::getenv("CROSSINK_READING_TEST_XTC_BOOK")) {
            // The real Library menu path: stats sync in place, then back to Library.
            PendingOverlayResume resume;
            resume.origin = PendingOverlayOrigin::Library;
            BookActions::syncProgress(renderer, mappedInputManager, "/read/sub/comic.xtc", std::move(resume));
            inputScript.clear();
            scriptIndex = 0;
            inputCompletionStep = SmokeStep::StatsUploadEmptyDone;
            inputScript.push_back(render("XTC Sync Book result", 100));
            inputScript.push_back(assertActivity("StatsUpload"));
            addTap(MappedInputManager::Button::Back);
            inputScript.push_back(assertActivity("Library"));
            step = SmokeStep::ReaderInput;
            break;
          } else if (std::getenv("CROSSINK_READING_TEST_ALL")) {
            // Sync All Books walks the Library index, not a folder.
            library::BuildStats stats;
            if (!library::buildLibraryIndex("/", stats)) fail("Cannot build Sync All Books Library fixture");
            // An indexed book deleted afterwards is skipped, not a failure.
            if (!Storage.remove("/read-sibling/outside.epub")) fail("Cannot remove indexed fixture book");
            if (std::getenv("CROSSINK_READING_TEST_STATS_ENTRY")) {
              // Reading Stats asks first, closes, then Sync All replaces the stack.
              activityManager.pushActivity(std::make_unique<StatsParentSmokeActivity>(renderer, mappedInputManager));
              inputScript.clear();
              scriptIndex = 0;
              inputCompletionStep = SmokeStep::StatsUploadEmptyDone;
              inputScript.push_back(render("Reading stats before Sync All", 5));
              addTap(MappedInputManager::Button::Confirm);
              inputScript.push_back(render("Sync All confirmation", 5));
              inputScript.push_back(assertActivity("Confirmation"));
              addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
              addTap(MappedInputManager::Button::Confirm);
              inputScript.push_back(render("Sync All from Reading Stats", 100));
              inputScript.push_back(assertActivity("StatsUpload"));
              addTap(MappedInputManager::Button::Back);
              inputScript.push_back(assertActivity("Home"));
              step = SmokeStep::ReaderInput;
              break;
            }
            activityManager.replaceActivity(std::make_unique<StatsUploadActivity>(renderer, mappedInputManager));
          } else
            activityManager.replaceActivity(
                std::make_unique<StatsUploadActivity>(renderer, mappedInputManager, "/read"));
          inputScript.clear();
          scriptIndex = 0;
          inputCompletionStep = SmokeStep::StatsUploadEmptyDone;
          if (currentBook) {
            inputScript.push_back(render("Current book combined sync", 40));
            step = SmokeStep::ReaderInput;
            break;
          }
          inputScript.push_back(render("Folder upload confirmation", 5));
          if (!std::getenv("CROSSINK_READING_TEST_CANCEL")) {
            addTap(MappedInputManager::Button::Confirm);
            if (std::getenv("CROSSINK_READING_TEST_ASK")) {
              for (int book = 0; book < 2; ++book) {
                inputScript.push_back(render("Folder sync choice", 25));
                inputScript.push_back(assertActivity("KOReaderSync"));
                if (std::getenv("CROSSINK_READING_TEST_ASK_CANCEL")) break;
                addTap(MappedInputManager::Button::Confirm);
              }
            }
            inputScript.push_back(render("Folder upload result", 100));
            if (std::getenv("CROSSINK_READING_TEST_SKIP_FAILED")) {
              const bool invalidBook = std::getenv("CROSSINK_READING_TEST_INVALID_BOOK");
              const int failures = std::getenv("CROSSINK_READING_TEST_ALL_FAIL") ? 2 : 1;
              for (int i = 0; i < failures; ++i) {
                inputScript.push_back(assertActivity(invalidBook ? "StatsUpload" : "KOReaderSync"));
                if (std::getenv("CROSSINK_READING_TEST_TOUCH")) {
#if CROSSINK_APP_CAP_TOUCH
                  if (!mappedInputManager.hasTouchHardware()) fail("Touch skip test requires a touch simulator");
                  const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
                  const auto& metrics = UITheme::getInstance().getMetrics();
                  const int x = area.x + area.width / 2;
                  const int y = area.y + area.height - (invalidBook ? 52 : metrics.verticalSpacing + 28);
                  inputScript.push_back(touchDown(x, y));
                  inputScript.push_back(touchRelease(x, y));
#else
                  fail("Touch skip test requires a touch simulator");
#endif
                } else {
                  addTap(MappedInputManager::Button::Confirm);
                }
                inputScript.push_back(render("Folder sync after skipping failed book", 100));
              }
              inputScript.push_back(assertActivity("StatsUpload"));
            } else if (std::getenv("CROSSINK_READING_TEST_ERROR")) {
              inputScript.push_back(assertActivity("KOReaderSync"));
            }
          }
          addTap(MappedInputManager::Button::Back);
          inputScript.push_back(assertActivity("Home"));
          step = SmokeStep::ReaderInput;
          break;
        }
        if (std::getenv("CROSSINK_STATS_TEST_EMPTY_LIBRARY")) {
          library::BuildStats stats;
          if (!Storage.mkdir("/empty-stats-library") || !library::buildLibraryIndex("/empty-stats-library", stats) ||
              stats.books || stats.folders)
            fail("Cannot build empty Library fixture");
          SETTINGS.trackReadingStats = 1;
          WiFi.mode(WIFI_STA);
          WiFi.begin("stats-smoke");
          activityManager.replaceActivity(std::make_unique<StatsUploadActivity>(renderer, mappedInputManager));
          inputScript.clear();
          scriptIndex = 0;
          inputCompletionStep = SmokeStep::StatsUploadEmptyDone;
          inputScript.push_back(render("Empty Library stats upload confirmation", 5));
          addTap(MappedInputManager::Button::Confirm);
          inputScript.push_back(render("Empty Library stats upload result", 100));
          addTap(MappedInputManager::Button::Back);
          inputScript.push_back(assertActivity("Home"));
          step = SmokeStep::ReaderInput;
          break;
        }
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_DICTIONARY")) verifyDictionaryElisions();
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_FEATURE")) {
          verifyStatusBarFeature();
          LOG_INF("SMOKE", "Simulator smoke test passed: status bar feature");
          std::_Exit(0);
        }
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_STATUS_BARS")) {
          verifyStatusBarSettings();
          SETTINGS.clockDateHasBeenSynced = true;
          SETTINGS.dateFormat = CrossPointSettings::DATE_FORMAT_YEAR_MONTH_DAY_NUMERIC;
          SETTINGS.dateSeparator = CrossPointSettings::DATE_SEPARATOR_HYPHEN;
          SETTINGS.displayStatusBar.slots = {ReaderStatusBarItem::Clock, ReaderStatusBarItem::Date,
                                             ReaderStatusBarItem::Battery};
          activityManager.replaceActivity(
              std::make_unique<StatusBarSettingsActivity>(renderer, mappedInputManager, false, false, true));
          queueStep("Display status bar editor", SmokeStep::StatusBarEditor, 4);
          break;
        }

        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_HOME_THEMES")) {
          if (!mappedInputManager.hasHomeKey() || !mappedInputManager.hasTouchHardware())
            fail("Home theme regression requires the X4 Pro simulator");
          for (const char* path : {"/books/theme-first.txt", "/books/theme-second.txt"}) {
            if (!Storage.writeFile(path, "Home theme fixture")) fail("Cannot create Home theme fixture");
            RECENT_BOOKS.addOrUpdateBook(path, path, {}, {}, RecentBook::CoverState::Missing);
          }
          SETTINGS.uiTheme = CrossPointSettings::LYRA_CAROUSEL;
          SETTINGS.uiScale = CrossPointSettings::UI_SCALE_SMALL;
          UITheme::getInstance().reload();
          homeThemeBookPath = "/books/theme-first.txt";
          activityManager.replaceActivity(std::make_unique<HomeActivity>(
              renderer, mappedInputManager, HomeMenuItem::NONE, HalDisplay::FAST_REFRESH, homeThemeBookPath));
          queueStep("Initial carousel Home", SmokeStep::ThemeHome, 8);
          break;
        }
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_LAYOUT")) {
          if (!mappedInputManager.hasHomeKey() || !mappedInputManager.hasTouchHardware())
            fail("Frontlight layout regression requires the X4 Pro simulator");
          activityManager.replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInputManager));
          queueStep("Frontlight layout Home", SmokeStep::FrontlightLayout, 4);
          break;
        }
        verifyStatusBarTextSizes();
        verifyLoadingPopupBackdrop();
        verifyCachedHomeProgressMigration();
        if (!CrossPointSettings::verifySleepTimeoutMigrationContract()) {
          fail("Sleep timeout migration contract failed");
        }
        if (!CrossPointSettings::verifySleepScreenMigrationContract()) {
          fail("Sleep screen migration contract failed");
        }
        if (!SimulatorHomeKeyInput::verifyTimingContract()) {
          fail("Simulator Home key timing contract failed");
        }
        verifyUpDownShortcutAvailability();
        verifySideButtonMigrationAndInput();
        verifyReaderControlsSettings();
        verifyStatusBarSettings();
        verifyMixedPageGestures();
#if CROSSINK_SCALABLE_FONTS
        verifyBlockFontSizes(sdFontSystem.ensureBuiltInReaderFont(renderer));
        if (const char* family = std::getenv("CROSSINK_SIMULATOR_SMOKE_FONT_FAMILY")) {
          // Exercise the production registry, adapter, size cache, and dictionary
          // handoff before the normal reader navigation smoke sequence.
          sdFontSystem.ensureRegistry();
          sdFontSystem.releaseRegistry();
          sdFontSystem.ensureRegistry();
          for (const auto& summary : sdFontSystem.registry().getFamilies()) {
            if (!summary.files.empty()) fail("Font names eagerly loaded detail paths");
          }
          const auto* info = sdFontSystem.registry().findFamily(family);
          if (!info || !info->isScalable()) fail("TTF family not discovered: %s", family);
          const auto picker = buildFontFamilySetting(&sdFontSystem.registry());
          const std::string expectedLabel = std::string(family) + " (8-22pt)";
          const auto item = std::find(picker.enumStringValues.begin(), picker.enumStringValues.end(), expectedLabel);
          if (item == picker.enumStringValues.end()) fail("TTF family missing from picker: %s", family);
          picker.valueSetter(static_cast<uint8_t>(item - picker.enumStringValues.begin()));
          if (std::strcmp(SETTINGS.sdFontFamilyName, family) != 0) fail("TTF picker selected the wrong family");
          SETTINGS.readerFontPointSize = 12;
          sdFontSystem.ensureLoaded(renderer);
          const int original = SETTINGS.getReaderFontId();
          verifyBlockFontSizes(original);
          const TtfRenderProfile initialProfile = TTF_RENDER_PROFILES.profileFor(family);
          TtfRenderProfile nativeProfile = initialProfile;
          nativeProfile.hinting = 1;  // TtfRenderProfile: Native
          nativeProfile.interpreter = 0;
          if (!TTF_RENDER_PROFILES.setProfile(family, nativeProfile)) fail("TTF native profile was not persisted");
          if (!sdFontSystem.reloadActiveScalableFamily(renderer, family)) fail("TTF native profile reload failed");
          const int nativeId = SETTINGS.getReaderFontId();
          if (nativeId == original || nativeId == SETTINGS.getBuiltInReaderFontId())
            fail("TTF native profile did not replace the resident font identity");
          if (!TTF_RENDER_PROFILES.setProfile(family, initialProfile)) fail("TTF initial profile was not restored");
          if (!sdFontSystem.reloadActiveScalableFamily(renderer, family)) fail("TTF initial profile reload failed");
          if (SETTINGS.getReaderFontId() != original) fail("TTF profile round trip changed font identity");
          if (ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Resident TTF family incorrectly requests loading feedback");
          }
          sdFontSystem.releaseLoadedFont(renderer);
          if (!ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Released TTF family suppressed loading feedback");
          }
          sdFontSystem.ensureLoaded(renderer);
          if (ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Reloaded TTF family incorrectly requests loading feedback");
          }
          sdFontSystem.releaseRegistry();
          if (!ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Released TTF catalog suppressed loading feedback");
          }
          sdFontSystem.ensureRegistry();
          if (ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Reloaded TTF catalog incorrectly requests loading feedback");
          }
          sdFontSystem.markRegistryDirty();
          if (!ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Dirty TTF family suppressed loading feedback");
          }
          sdFontSystem.ensureLoaded(renderer);
          if (ReaderUtils::shouldShowFontPreviewLoading(family)) {
            fail("Reloaded dirty TTF family incorrectly requests loading feedback");
          }
          SETTINGS.readerFontPointSize = 22;
          sdFontSystem.releaseRegistry();
          if (ReaderUtils::changeReaderFontSizeWithFeedback(renderer, true, FontSizeStepMode::Clamp) ||
              SETTINGS.readerFontPointSize != 22)
            fail("Cold resize at maximum size did not remain clamped");
          SETTINGS.readerFontPointSize = 12;
          sdFontSystem.releaseRegistry();
          if (!ReaderUtils::changeReaderFontSizeWithFeedback(renderer, true, FontSizeStepMode::Clamp) ||
              SETTINGS.readerFontPointSize != 13)
            fail("Resize did not lazily reload indexed SD sizes");
          sdFontSystem.ensureLoaded(renderer);
          if (!ReaderUtils::changeReaderFontSizeWithFeedback(renderer, false, FontSizeStepMode::Clamp) ||
              SETTINGS.readerFontPointSize != 12)
            fail("Reverse resize did not use indexed sizes");
          sdFontSystem.ensureLoaded(renderer);
          if (original == SETTINGS.getBuiltInReaderFontId()) fail("TTF activation fell back");
          for (uint8_t points : {uint8_t(14), uint8_t(12)}) {
            SETTINGS.readerFontPointSize = points;
            sdFontSystem.ensureLoaded(renderer);
            if (SETTINGS.getReaderFontId() == SETTINGS.getBuiltInReaderFontId()) fail("TTF size activation failed");
          }
          if (SETTINGS.getReaderFontId() != original) fail("TTF size identity changed on reuse");
          if (std::getenv("CROSSINK_SIMULATOR_SMOKE_ISOLATED_FONTS")) {
            // A clean resize must use resident metadata, even with the cache temporarily unavailable.
            namespace fs = std::filesystem;
            fs::rename("fs_/.crosspoint/font-catalog.bin", "fs_/.crosspoint/font-catalog.saved");
            if (!ReaderUtils::changeReaderFontSizeWithFeedback(renderer, true, FontSizeStepMode::Clamp))
              fail("Clean resize failed");
            sdFontSystem.ensureLoaded(renderer);
            if (fs::exists("fs_/.crosspoint/font-catalog.bin")) fail("Clean resize reread/rebuilt the index");
            fs::rename("fs_/.crosspoint/font-catalog.saved", "fs_/.crosspoint/font-catalog.bin");
            if (!ReaderUtils::changeReaderFontSizeWithFeedback(renderer, false, FontSizeStepMode::Clamp))
              fail("Clean reverse resize failed");
            sdFontSystem.ensureLoaded(renderer);
          }
          const auto dictionary = sdFontSystem.activateDictionaryFont(renderer, family, 16);
          if (!dictionary.usingDictionaryFont) fail("TTF dictionary activation failed");
          if (sdFontSystem.restoreReaderFont(renderer) != original) fail("TTF reader restoration changed identity");
          // Cold dictionary activation has no persistent book-layout identity.
          // Restoring that same family must rebuild its normal reader identity.
          sdFontSystem.releaseLoadedFont(renderer);
          const auto temporaryDictionary = sdFontSystem.activateDictionaryFont(renderer, family, 12);
          if (!temporaryDictionary.usingDictionaryFont || temporaryDictionary.fontId == original)
            fail("Cold TTF dictionary did not use a temporary identity");
          const auto reusedDictionary = sdFontSystem.activateDictionaryFont(renderer, family, 12);
          if (reusedDictionary.fontId != temporaryDictionary.fontId)
            fail("Active TTF dictionary family was not reused");
          if (sdFontSystem.restoreReaderFont(renderer) != original)
            fail("Temporary dictionary identity escaped into reader restoration");
          sdFontSystem.releaseLoadedFont(renderer);
          if (!sdFontSystem.activateDictionaryFont(renderer, family, 12).usingDictionaryFont)
            fail("Cold TTF dictionary reload failed");
          sdFontSystem.ensureLoaded(renderer);
          if (SETTINGS.getReaderFontId() != original)
            fail("Reader ensureLoaded reused a temporary dictionary identity");
          sdFontSystem.releaseLoadedFont(renderer);
          sdFontSystem.ensureLoaded(renderer);
          if (SETTINGS.getReaderFontId() != original) fail("TTF reload changed identity");
          if (std::getenv("CROSSINK_SIMULATOR_SMOKE_ISOLATED_FONTS")) {
            namespace fs = std::filesystem;
            // The runner provides disposable copies; never mutate a user's SD tree.
            const auto* current = sdFontSystem.registry().findFamily(family);
            const std::string firstPath = "fs_" + current->files.front().path;
            fs::copy("fs_/fonts", "fs_/font-smoke-backup", fs::copy_options::recursive);
            sdFontSystem.releaseLoadedFont(renderer);
            // Budget failures must preserve selection, release partial faces,
            // and permit a subsequent valid load without stale renderer IDs.
            const auto originalBytes = fs::file_size(firstPath);
            fs::resize_file(firstPath, HalScalableFont::MaxFileBytes + 1);
            sdFontSystem.ensureLoaded(renderer);
            if (SETTINGS.getReaderFontId() != SETTINGS.getBuiltInReaderFontId() ||
                std::strcmp(SETTINGS.sdFontFamilyName, family) != 0)
              fail("Oversized TTF file did not preserve selection and fall back");
            fs::resize_file(firstPath, originalBytes);
            if (current->files.size() == 4) {
              for (const auto& face : current->files) fs::resize_file("fs_" + face.path, HalScalableFont::MaxFileBytes);
              sdFontSystem.ensureLoaded(renderer);
              if (SETTINGS.getReaderFontId() != SETTINGS.getBuiltInReaderFontId() ||
                  std::strcmp(SETTINGS.sdFontFamilyName, family) != 0)
                fail("Oversized TTF family did not preserve selection and fall back");
              for (const auto& face : current->files) {
                const fs::path target = "fs_" + face.path;
                const auto backup = fs::path("fs_/font-smoke-backup") / fs::relative(target, "fs_/fonts");
                fs::copy_file(backup, target, fs::copy_options::overwrite_existing);
              }
            }
            sdFontSystem.ensureLoaded(renderer);
            if (SETTINGS.getReaderFontId() != original) fail("TTF budget failure did not recover");
            sdFontSystem.releaseLoadedFont(renderer);
            {
              std::ofstream changed(firstPath, std::ios::binary | std::ios::app);
              changed.put('X');
            }
            sdFontSystem.markRegistryDirty();
            sdFontSystem.refreshIfDirty();  // A picker must not consume the active-font reload signal.
            sdFontSystem.ensureLoaded(renderer);
            if (SETTINGS.getReaderFontId() == original ||
                SETTINGS.getReaderFontId() == SETTINGS.getBuiltInReaderFontId())
              fail("TTF replacement did not change content identity");
            sdFontSystem.releaseLoadedFont(renderer);
            // Reproduce the source location without interpreting font metadata as a path.
            const fs::path sourceFolder = fs::path(firstPath).parent_path();
            const fs::path duplicateFolder = sourceFolder == fs::path("fs_/fonts")
                                                 ? fs::path("fs_/.fonts")
                                                 : fs::path("fs_/.fonts") / sourceFolder.filename();
            fs::create_directories(duplicateFolder);
            fs::copy_file(firstPath, duplicateFolder / "duplicate.ttf", fs::copy_options::overwrite_existing);
            constexpr const char* separateFamily = "TTF Smoke Separate Family";
            const fs::path separateFolder = fs::path("fs_/fonts") / separateFamily;
            fs::create_directories(separateFolder);
            fs::copy_file(firstPath, separateFolder / "same-metadata.ttf");
            sdFontSystem.markRegistryDirty();
            sdFontSystem.refreshIfDirty();
            if (!sdFontSystem.registry().findFamily(separateFamily)) fail("TTF folder name was ignored");
            FontInstaller installer(sdFontSystem.registry());
            if (installer.deleteFamily(family) != FontInstaller::Error::OK) fail("TTF family deletion failed");
            sdFontSystem.markRegistryDirty();
            sdFontSystem.ensureLoaded(renderer);
            sdFontSystem.refreshIfDirty();
            if (sdFontSystem.registry().findFamily(family) || SETTINGS.sdFontFamilyName[0])
              fail("Deleted TTF family reappeared from duplicate files");
            if (!sdFontSystem.registry().findFamily(separateFamily) ||
                !fs::exists(separateFolder / "same-metadata.ttf"))
              fail("Deleting TTF family removed a different folder with the same metadata");
            fs::remove_all("fs_/fonts");
            fs::rename("fs_/font-smoke-backup", "fs_/fonts");
            sdFontSystem.markRegistryDirty();
            std::snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "%s", family);
            sdFontSystem.ensureLoaded(renderer);
            if (SETTINGS.getReaderFontId() != original) fail("Restored TTF content changed identity");
            LOG_INF("SMOKE", "TTF replacement, duplicate deletion and restoration passed");
          }
          LOG_INF("SMOKE", "TTF discovery, size reuse, dictionary and reload passed");
        }
#endif
        applyRequestedTheme();
        homeReaderReaderKind = 0;
        homeReaderCancelledMask = 0;
        homeReaderConfirmationAccepted = false;
        activityManager.replaceActivity(std::make_unique<HomeReaderSmokeActivity>(
            homeReaderSmokeReaderName(homeReaderReaderKind), true, renderer, mappedInputManager, true));
        queueStep("Home/Reader reader owner", SmokeStep::HomeReaderReader);
        break;

      case SmokeStep::HomeReaderReader:
        homeReaderSmokeReader = activityManager.simulatorCurrentActivity();
        if (!homeReaderSmokeReader || !homeReaderSmokeReader->isBookReaderActivity()) {
          fail("Home/Reader smoke reader did not become current");
        }
        homeReaderCancelledMask = 0;
        homeReaderSmokeReader->startActivityForResult(
            std::make_unique<HomeReaderSmokeActivity>("ReaderMenu", true, renderer, mappedInputManager),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) fail("Home/Reader confirmed the first nested menu");
              homeReaderCancelledMask |= 1;
            });
        queueStep("Home/Reader nested menu", SmokeStep::HomeReaderNested);
        break;

      case SmokeStep::HomeReaderNested:
        activityManager.simulatorCurrentActivity()->startActivityForResult(
            std::make_unique<HomeReaderSmokeActivity>("NestedSettings", false, renderer, mappedInputManager),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) fail("Home/Reader confirmed nested settings");
              homeReaderCancelledMask |= 2;
            });
        queueStep("Home/Reader destructive confirmation", SmokeStep::HomeReaderConfirmation);
        break;

      case SmokeStep::HomeReaderConfirmation:
        activityManager.simulatorCurrentActivity()->startActivityForResult(
            std::make_unique<HomeReaderSmokeActivity>("Confirmation", false, renderer, mappedInputManager),
            [this](const ActivityResult& result) {
              homeReaderConfirmationAccepted = !result.isCancelled;
              homeReaderCancelledMask |= 4;
            });
        queueStep("Home/Reader trigger", SmokeStep::HomeReaderTrigger);
        break;

      case SmokeStep::HomeReaderTrigger:
        if (!activityManager.handleShortcutAction(CrossPointSettings::HOME_READER) ||
            !activityManager.handleShortcutAction(CrossPointSettings::HOME_READER)) {
          fail("Repeated Home/Reader press was not consumed");
        }
        queueStep("Home/Reader unwound to reader", SmokeStep::HomeReaderUnwound);
        break;

      case SmokeStep::HomeReaderUnwound:
        if (activityManager.simulatorCurrentActivity() != homeReaderSmokeReader || homeReaderCancelledMask != 7 ||
            homeReaderConfirmationAccepted) {
          fail("Home/Reader did not cancel every nested screen back to the existing reader");
        }
        if (!activityManager.handleShortcutAction(CrossPointSettings::HOME_READER)) {
          fail("Home/Reader from the reader was not consumed");
        }
        queueStep("Home/Reader reader to Home", SmokeStep::HomeReaderReturnedHome);
        break;

      case SmokeStep::HomeReaderReturnedHome: {
        if (!activityManager.isHomeActivity()) fail("Home/Reader from the reader did not return Home");
        Activity* const homeBeforeNoOp = activityManager.simulatorCurrentActivity();
        if (!activityManager.handleShortcutAction(CrossPointSettings::HOME_READER) ||
            activityManager.simulatorCurrentActivity() != homeBeforeNoOp) {
          fail("Home/Reader was not a no-op on Home");
        }
        if (++homeReaderReaderKind < 3) {
          activityManager.replaceActivity(std::make_unique<HomeReaderSmokeActivity>(
              homeReaderSmokeReaderName(homeReaderReaderKind), true, renderer, mappedInputManager, true));
          queueStep("Home/Reader next reader type", SmokeStep::HomeReaderReader);
        } else {
          activityManager.simulatorCurrentActivity()->startActivityForResult(
              std::make_unique<HomeReaderSmokeActivity>("Settings", false, renderer, mappedInputManager),
              [](const ActivityResult&) {});
          queueStep("Home/Reader non-reader parent", SmokeStep::HomeReaderNonReaderParent);
        }
        break;
      }

      case SmokeStep::HomeReaderNonReaderParent:
        activityManager.simulatorCurrentActivity()->startActivityForResult(
            std::make_unique<HomeReaderSmokeActivity>("Browser", false, renderer, mappedInputManager),
            [](const ActivityResult&) {});
        queueStep("Home/Reader non-reader nested", SmokeStep::HomeReaderNonReaderNested);
        break;

      case SmokeStep::HomeReaderNonReaderNested:
        if (!activityManager.handleShortcutAction(CrossPointSettings::HOME_READER) ||
            !activityManager.handleShortcutAction(CrossPointSettings::HOME_READER)) {
          fail("Home/Reader outside a reader was not consumed");
        }
        queueStep("Home/Reader non-reader to Home", SmokeStep::HomeReaderNonReaderHome);
        break;

      case SmokeStep::HomeReaderNonReaderHome:
        if (!activityManager.isHomeActivity()) fail("Home/Reader from non-reader menus did not return Home");
        activityManager.replaceActivity(
            std::make_unique<HomeReaderSmokeActivity>("EpubReader", true, renderer, mappedInputManager, true));
        queueStep("Back/Home regression reader", SmokeStep::BackHomeReader);
        break;

      case SmokeStep::BackHomeReader:
        backHomeChildCancelled = false;
        activityManager.simulatorCurrentActivity()->startActivityForResult(
            std::make_unique<HomeReaderSmokeActivity>("ReaderMenu", false, renderer, mappedInputManager),
            [this](const ActivityResult& result) { backHomeChildCancelled = result.isCancelled; });
        queueStep("Back/Home nested menu", SmokeStep::BackHomeNested);
        break;

      case SmokeStep::BackHomeNested:
        if (!activityManager.handleShortcutAction(CrossPointSettings::BACK_HOME))
          fail("Back/Home did not handle nested reader menu");
        queueStep("Back/Home returned one level", SmokeStep::BackHomeReturnedReader);
        break;

      case SmokeStep::BackHomeReturnedReader:
        if (!activityManager.simulatorCurrentActivity()->isReaderActivity() || !backHomeChildCancelled) {
          fail("Back/Home no longer pops exactly one canceled nested activity");
        }
        if (!activityManager.handleShortcutAction(CrossPointSettings::BACK_HOME))
          fail("Back/Home did not return from the reader");
        queueStep("Back/Home returned Home", SmokeStep::BackHomeReturnedHome);
        break;

      case SmokeStep::BackHomeReturnedHome:
        if (!activityManager.isHomeActivity()) fail("Back/Home from the reader did not return Home");
        savedNavigationMenu = SETTINGS.longPressMenuAction;
        savedNavigationBack = SETTINGS.longPressBackAction;
        navigationLongPass = 0;
        activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"), true);
        queueStep("Navigation long-press reader", SmokeStep::NavigationLongReader, 8);
        break;

      case SmokeStep::NavigationLongReader: {
        if (!activityManager.isCurrentActivityNamed("EpubReader")) fail("Navigation long-press reader did not open");
        const bool back = navigationLongPass >= 2;
        const bool homeReader = navigationLongPass % 2 != 0;
        SETTINGS.longPressMenuAction = back         ? CrossPointSettings::LONG_MENU_OFF
                                       : homeReader ? CrossPointSettings::LONG_MENU_HOME_READER
                                                    : CrossPointSettings::LONG_MENU_BACK_HOME;
        SETTINGS.longPressBackAction =
            back ? homeReader ? CrossPointSettings::LONG_MENU_HOME_READER : CrossPointSettings::LONG_MENU_BACK_HOME
                 : CrossPointSettings::LONG_MENU_OFF;
        mappedInputManager.simulatorInjectPress(back ? MappedInputManager::Button::Back
                                                     : MappedInputManager::Button::Confirm);
        navigationPressAt = millis();
        step = SmokeStep::NavigationLongHeld;
        break;
      }

      case SmokeStep::NavigationLongHeld:
        if (millis() - navigationPressAt < 1400) break;
        if (!activityManager.isHomeActivity())
          fail("Navigation hold did not return Home (pass %u)", navigationLongPass);
        navigationExpected = activityManager.simulatorCurrentActivity();
        mappedInputManager.simulatorInjectRelease(navigationLongPass >= 2 ? MappedInputManager::Button::Back
                                                                          : MappedInputManager::Button::Confirm);
        queueStep("Navigation release consumed", SmokeStep::NavigationLongReleased);
        break;

      case SmokeStep::NavigationLongReleased: {
        if (activityManager.simulatorCurrentActivity() != navigationExpected)
          fail("Navigation release leaked into the destination screen (pass %u)", navigationLongPass);
        if (++navigationLongPass < 4) {
          activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"), true);
          queueStep("Next navigation long-press", SmokeStep::NavigationLongReader, 8);
        } else {
          SETTINGS.longPressMenuAction = savedNavigationMenu;
          SETTINGS.longPressBackAction = savedNavigationBack;
          activityManager.replaceActivity(
              std::make_unique<NavigationPickerSmokeActivity>(renderer, mappedInputManager));
          queueStep("Navigation shortcut picker", SmokeStep::NavigationPicker);
        }
        break;
      }

      case SmokeStep::NavigationPicker:
        if (const char* path = std::getenv("CROSSINK_SIMULATOR_SMOKE_NAVIGATION_CAPTURE")) {
          FILE* image = std::fopen(path, "wb");
          if (!image) fail("Cannot create navigation picker capture");
          RenderLock lock;
          const int width = renderer.getScreenWidth();
          const int height = renderer.getScreenHeight();
          std::fprintf(image, "P5\n%d %d\n255\n", width, height);
          for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
          }
          std::fclose(image);
        }
        LOG_INF("SMOKE", "Navigation catalogs, persistence, nested menus, holds and releases passed");
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_NAVIGATION_ONLY")) {
          LOG_INF("SMOKE", "Simulator smoke test passed: navigation shortcuts");
          std::_Exit(0);
        }
        activityManager.goHome();
        queueStep("Home", SmokeStep::Home);
        break;

      case SmokeStep::Home:
        activityManager.goToFileBrowser("/books");
        queueStep("File Browser", SmokeStep::FileBrowser);
        break;

      case SmokeStep::FileBrowser:
#if CROSSINK_APP_CAP_TOUCH
        if (mappedInputManager.hasTouchHardware()) {
          buildFileBrowserInputScript();
          step = SmokeStep::ReaderInput;
          break;
        }
#endif
        prepareRecentLibrary();
        activityManager.goToLibrary();
        queueStep("Recent Library", SmokeStep::RecentLibrary);
        break;

      case SmokeStep::FileBrowserSettings:
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_SYNC")) {
          if (!mappedInputManager.hasHomeKey()) fail("Frontlight sync regression requires X4 Pro simulator");
          LOG_INF("SMOKE", "Frontlight sync: stats on/off menu and dispatch checks passed");
          step = SmokeStep::Done;
          break;
        }
        prepareRecentLibrary();
        activityManager.goToLibrary();
        queueStep("Recent Library", SmokeStep::RecentLibrary);
        break;

      case SmokeStep::RecentLibrary: {
        RenderLock lock;
        auto* activity = static_cast<LibraryActivity*>(activityManager.simulatorCurrentActivity());
        if (Storage.exists(library::libraryIndexPath()) || !library::libraryIndexNeedsRefresh())
          fail("Recently Opened built the missing Library index");
        RecentBook book;
        if (activity->simulatorRowCount() != 18 || !activity->simulatorReadBook(0, book) ||
            book.path != "/books/recent-smoke-19.txt")
          fail("Recent Library did not show bounded history in newest-first order");
        activity->simulatorSetView(4, false);
        if (!activity->simulatorReadBook(0, book) || book.path != "/books/recent-smoke-2.txt")
          fail("Recent Library did not reverse history");
        activity->simulatorSetView(4, true, "Author 19");
        if (activity->simulatorRowCount() != 1 || !activity->simulatorReadBook(0, book) ||
            book.path != "/books/recent-smoke-19.txt")
          fail("Recent Library author search failed");
        SETTINGS.libraryShowTxt = 0;
        activity->simulatorSetView(4, true);
        if (activity->simulatorRowCount() != 0) fail("Recent Library file filter failed");
        SETTINGS.libraryShowTxt = 1;
        SETTINGS.libraryUseMetadata = 0;
        activity->simulatorSetView(4, true, "recent-smoke-19");
        if (activity->simulatorRowCount() != 1 || !activity->simulatorReadBook(0, book) ||
            book.title != "recent-smoke-19" || !book.author.empty())
          fail("Recent Library filename display/search failed");
        SETTINGS.libraryUseMetadata = 1;
        // An unreadable index must also be irrelevant to a history refresh.
        if (!Storage.writeFile(library::libraryIndexPath(), "broken")) fail("Cannot write broken index fixture");
        activity->simulatorSetView(4, true);
        activity->simulatorRefresh();
        FsFile broken;
        if (!Storage.openFileForRead("SMOKE", library::libraryIndexPath(), broken)) fail("Missing broken index");
        const auto brokenSize = broken.size();
        broken.close();
        if (brokenSize != 6 || !library::libraryIndexNeedsRefresh() || activity->simulatorRowCount() != 18)
          fail("Recent Library refreshed the full index");
        activity->simulatorSetView(1, false);
        activity->simulatorSetView(4, true);
        if (activity->simulatorRowCount() != 18 || !activity->simulatorReadBook(0, book))
          fail("Recent Library stayed unavailable after a failed full scan");
        const char* epubPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (epubPath) {
          const std::string cachePath = Epub(epubPath, "/.crosspoint").getCachePath();
          if (!Storage.exists(cachePath.c_str()) && !Storage.mkdir(cachePath.c_str()))
            fail("Cannot create completed-book cache");
          auto stats = BookReadingStats::load(cachePath);
          stats.isCompleted = true;
          if (!stats.save(cachePath)) fail("Cannot save completed-book fixture");
          RECENT_BOOKS.addOrUpdateBook(epubPath, "Completed smoke book", "", "");
          SETTINGS.libraryHideFinishedBooks = 1;
          activity->simulatorSetView(4, true);
          if (activity->simulatorRowCount() != 17) fail("Recent Library did not hide finished EPUB");
          SETTINGS.libraryHideFinishedBooks = 0;
          activity->simulatorSetView(4, true);
          if (activity->simulatorRowCount() != 18 || !activity->simulatorReadBook(0, book) || book.path != epubPath)
            fail("Recent Library did not restore finished EPUB");
          RECENT_BOOKS.removeByPath(epubPath);
          stats.isCompleted = false;
          if (!stats.save(cachePath)) fail("Cannot restore completed-book fixture");
        }
        for (int i = 0; i < 20; ++i) {
          const std::string path = "/books/recent-smoke-" + std::to_string(i) + ".txt";
          if (!Storage.remove(path.c_str())) fail("Cannot remove recent fixture");
        }
        activity->simulatorRefresh();
        if (activity->simulatorRowCount() != 0) fail("Recent Library did not prune missing books");
        // The builder deliberately retains an unreadable previous index. Remove
        // that fixture before checking a deferred build from a missing index.
        if (!Storage.remove(library::libraryIndexPath())) fail("Cannot remove broken index fixture");
        // Switching to a full-library sort must still build the deferred index.
        activity->simulatorSetView(1, false);
        if (library::libraryIndexNeedsRefresh() || activity->simulatorRowCount() == 0)
          fail("Full Library did not build its deferred index");
        SETTINGS.librarySortMethod = 1;
        SETTINGS.librarySortDescending = 0;
        LOG_INF("SMOKE",
                "Recent Library missing/corrupt index, 18-book limit, ordering, search, filters and completion passed");
        queueStep("Library", SmokeStep::Library);
        break;
      }

      case SmokeStep::Library: {
        // Rendering an error screen is not a successful Library smoke test.
        // The script supplies an isolated card with at least one EPUB.
        library::LibraryIndexFile shelf;
        const bool hasFixture = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK") != nullptr;
        const bool readable = shelf.open(library::libraryIndexPath());
        const bool populated = readable && (!hasFixture || shelf.bookCount() > 0);
        const uint16_t books = shelf.bookCount();
        shelf.close();
        if (!populated) fail("Library did not publish a readable populated index");
        constexpr char REFRESH_FIXTURE[] = "/books/library-refresh-smoke.txt";
        if (library::libraryIndexNeedsRefresh()) fail("Successful Library scan stayed dirty");
        if (libraryRefreshPass == 0) {
          libraryBaselineBooks = books;
          auto* libraryActivity = static_cast<LibraryActivity*>(activityManager.simulatorCurrentActivity());
          const int beforeSelection = libraryActivity->simulatorSelection();
          {
            RenderLock busyRender;
            mappedInputManager.simulatorClearInputFrame();
            mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
            libraryActivity->loop();
            mappedInputManager.simulatorClearInputFrame();
            mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Up);
            libraryActivity->loop();
            if (libraryActivity->simulatorPendingInputs() != 2 ||
                libraryActivity->simulatorSelection() != beforeSelection)
              fail("Library did not buffer navigation while rendering");
#if CROSSINK_APP_CAP_TOUCH
            mappedInputManager.simulatorClearInputFrame();
            mappedInputManager.simulatorInjectTouchDown(200, 300);
            libraryActivity->loop();
            mappedInputManager.simulatorClearInputFrame();
            mappedInputManager.simulatorInjectTouchMove(200, 100);
            libraryActivity->loop();
            mappedInputManager.simulatorClearInputFrame();
            mappedInputManager.simulatorInjectTouchRelease(200, 100);
            libraryActivity->loop();
            if (libraryActivity->simulatorPendingInputs() != 5)
              fail("Library did not buffer touch press, cancellation and scroll");
#endif
          }
          mappedInputManager.simulatorClearInputFrame();
          while (libraryActivity->simulatorPendingInputs()) libraryActivity->loop();
          const int selectionBeforeOverflow = libraryActivity->simulatorSelection();
          {
            RenderLock busyRender;
            for (size_t i = 0; i <= LibraryInputBuffer::CAPACITY; ++i) {
              mappedInputManager.simulatorClearInputFrame();
              mappedInputManager.simulatorInjectRelease(MappedInputManager::Button::Down);
              libraryActivity->loop();
            }
            if (libraryActivity->simulatorPendingInputs() != 1)
              fail("Library overflow did not cancel the partial input sequence");
          }
          mappedInputManager.simulatorClearInputFrame();
          libraryActivity->loop();
          if (libraryActivity->simulatorSelection() != selectionBeforeOverflow)
            fail("Library replayed navigation after input overflow");
          LOG_INF("SMOKE", "Library input buffering during rendering and overflow passed");
          // Deliberately bypass invalidation to prove that a normal return visit
          // reuses the index instead of walking the card again.
          if (!Storage.writeFile(REFRESH_FIXTURE, "Library refresh smoke fixture"))
            fail("Cannot create Library fixture");
        } else if (libraryRefreshPass == 1) {
          if (books != libraryBaselineBooks) fail("Library rescanned an unchanged session");
          library::invalidateLibraryIndex();
        } else if (libraryRefreshPass == 2) {
          if (books != libraryBaselineBooks + 1) fail("Library missed an invalidated addition");
          if (!Storage.remove(REFRESH_FIXTURE)) fail("Cannot delete Library fixture");
          library::invalidateLibraryIndex();
        } else if (books != libraryBaselineBooks) {
          fail("Library missed an invalidated deletion");
        }
        if (libraryRefreshPass++ < 3) {
          activityManager.goToLibrary();
          queueStep("Library cache reuse and invalidation", SmokeStep::Library);
          break;
        }
        LOG_INF("SMOKE", "Library reuse, addition and deletion refresh passed");
        if (mappedInputManager.hasHomeKey()) {
          renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
        }
        KOREADER_STORE.setCredentials("smoke", "smoke-password");
        activityManager.replaceActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
        queueStep("Sync Settings", SmokeStep::StatsUploadEntry);
        break;
      }

      case SmokeStep::StatsUploadEntry:
        inputScript.clear();
        scriptIndex = 0;
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_SYNC_SERVER_CAPTURES")) {
          // Capture-only pass: top of the list, the scrolled sync groups, then a KOSync-only server.
          {
            RenderLock lock;
            captureStatusBarScreen("sync-server-1-top");
          }
          inputCompletionStep = SmokeStep::SyncServerCaptureBottom;
          for (int i = 0; i < 6; ++i) addTap(MappedInputManager::Button::Down);
          inputScript.push_back(render("Sync Server scrolled", 6));
          step = SmokeStep::ReaderInput;
          break;
        }
        inputCompletionStep = SmokeStep::StatsUploadReturn;
        for (int i = 0; i < 5; ++i) addTap(MappedInputManager::Button::Down);  // Sync All Books.
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(assertActivity("StatsUpload"));
        inputScript.push_back(render("Sync All Books confirmation", 10));
        addTap(MappedInputManager::Button::Back);  // Opening does not upload.
        inputScript.push_back(assertActivity("Home"));
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::SyncServerCaptureBottom: {
        {
          RenderLock lock;
          captureStatusBarScreen("sync-server-2-what-to-sync");
        }
        KOREADER_STORE.setServerUrl("http://kosync.invalid");
        KOREADER_STORE.setServerSupport(SyncServerSupport::UNSUPPORTED);
        activityManager.requestUpdate();
        queueStep("Sync Server progress-only", SmokeStep::SyncServerCaptureUnsupported, 6);
        break;
      }

      case SmokeStep::SyncServerCaptureUnsupported: {
        {
          RenderLock lock;
          captureStatusBarScreen("sync-server-3-progress-only");
        }
        // Reading Stats "This Device" page offers Sync All Books once an account exists.
        GlobalReadingStats device;
        device.totalSessions = 12;
        device.totalReadingSeconds = 3456;
        activityManager.replaceActivity(std::make_unique<BookStatsActivity>(
            renderer, mappedInputManager, "Fixture", std::string{}, BookReadingStats{}, -1.0f, false, 0, device));
        queueStep("Reading stats sync action", SmokeStep::SyncServerCaptureStats, 6);
        break;
      }

      case SmokeStep::SyncServerCaptureStats: {
        {
          RenderLock lock;
          captureStatusBarScreen("sync-server-4-stats-page");
        }
        LOG_INF("SMOKE", "Simulator smoke test passed: Sync Server captures");
        std::_Exit(0);
      }

      case SmokeStep::StatsUploadEmptyDone:
        if (std::getenv("CROSSINK_READING_TEST_STATS_ENTRY") && !statsParentHandlerRan)
          fail("Reading Stats parent handler did not run before Sync All replaced the stack");
        LOG_INF("SMOKE", "Stats upload transport smoke passed");
        std::_Exit(0);

      case SmokeStep::StatsUploadReturn:
        KOREADER_STORE.setCredentials("", "");
        LOG_INF("SMOKE", "Sync All Books entry and cancellation passed");
        activityManager.goToSettings();
        queueStep(mappedInputManager.hasHomeKey() ? "Settings landscape" : "Settings", SmokeStep::Settings);
        break;

      case SmokeStep::StatusBarEditor: {
        {
          RenderLock lock;
          captureStatusBarScreen("display-editor");
        }
        inputScript = {press(MappedInputManager::Button::Confirm), release(MappedInputManager::Button::Confirm),
                       render("Display status bar picker", 4)};
        scriptIndex = 0;
        inputCompletionStep = SmokeStep::StatusBarPicker;
        step = SmokeStep::ReaderInput;
        break;
      }
      case SmokeStep::StatusBarPicker: {
        RenderLock lock;
        captureStatusBarScreen("display-picker");
        for (const uint8_t theme : {CrossPointSettings::CLASSIC, CrossPointSettings::MINIMAL, CrossPointSettings::LYRA,
                                    CrossPointSettings::ROUNDEDRAFF, CrossPointSettings::DASHBOARD}) {
          SETTINGS.uiTheme = theme;
          UITheme::getInstance().reload();
          const auto& metrics = UITheme::getInstance().getMetrics();
          for (unsigned format = 0; format < CrossPointSettings::DATE_FORMAT_COUNT; ++format) {
            SETTINGS.dateFormat = format;
            renderer.clearScreen();
            GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                           tr(STR_SETTINGS_TITLE));
            const auto name = "header-" + std::to_string(theme) + "-date-" + std::to_string(format);
            captureStatusBarScreen(name.c_str());
          }
        }
        LOG_INF("SMOKE", "Simulator smoke test passed: status bar editor, picker and themed date headers");
        std::_Exit(0);
      }

      case SmokeStep::Settings:
        renderer.setOrientation(GfxRenderer::Orientation::Portrait);
        if (!mappedInputManager.hasTouchHardware()) {
          inputScript.clear();
          scriptIndex = 0;
          inputCompletionStep = SmokeStep::SideButtons;
          const auto down = mappedInputManager.menuButton(MappedInputManager::Button::Down);
          const auto up = mappedInputManager.menuButton(MappedInputManager::Button::Up);
          const auto left = mappedInputManager.menuButton(MappedInputManager::Button::Left);
          const auto right = mappedInputManager.menuButton(MappedInputManager::Button::Right);
          addTap(right);
          inputScript.push_back(render("Settings Right selects Reader tab", 3));
          inputScript.push_back(assertSettingsNavigation(1, 0));
          addTap(down);
          inputScript.push_back(render("Settings Down selects first row", 3));
          inputScript.push_back(assertSettingsNavigation(1, 1));
          addTap(left);
          inputScript.push_back(render("Settings Left switches tabs while a row is focused", 3));
          inputScript.push_back(assertSettingsNavigation(0, 1));
          addTap(up);
          inputScript.push_back(render("Settings Up returns to tab band", 3));
          inputScript.push_back(assertSettingsNavigation(0, 0));
          addTap(right);  // Reader tab
          addTap(right);  // Controls tab
          const int controlsCount = static_cast<int>(buildControlsSettingsParentList(getSettingsList()).size());
          for (int i = 0; i < controlsCount; ++i) addTap(down);
          inputScript.push_back(render("Menu navigation last in Controls", 3));
          inputScript.push_back({ScriptActionType::CaptureMenuNavigation, down, "menu-navigation-controls", 0, 0, 0});
          addTap(MappedInputManager::Button::Confirm);
          inputScript.push_back(render("Menu navigation picker", 3));
          inputScript.push_back({ScriptActionType::CaptureMenuNavigation, down, "menu-navigation-picker", 0, 0, 0});
          addTap(MappedInputManager::Button::Right);
          inputScript.push_back(render("Legacy navigation note", 3));
          inputScript.push_back(
              {ScriptActionType::CaptureMenuNavigation, down, "menu-navigation-picker-legacy", 0, 0, 0});
          addTap(MappedInputManager::Button::Confirm);
          inputScript.push_back(render("Classic selected without moving the row", 3));
          inputScript.push_back(
              {ScriptActionType::AssertMenuNavigation, down, nullptr, 0, CrossPointSettings::MENU_NAV_CLASSIC, 0});
          inputScript.push_back(assertSettingsNavigation(2, controlsCount));
          addTap(MappedInputManager::Button::Right);
          inputScript.push_back(assertSettingsNavigation(2, 0));
          addTap(MappedInputManager::Button::Left);
          inputScript.push_back(assertSettingsNavigation(2, controlsCount));
          addTap(MappedInputManager::Button::Down);
          inputScript.push_back(assertSettingsNavigation(2, 0));
          addTap(MappedInputManager::Button::Up);
          inputScript.push_back(assertSettingsNavigation(2, controlsCount));
          addTap(MappedInputManager::Button::Right);
          inputScript.push_back(assertSettingsNavigation(2, 0));
          addTap(MappedInputManager::Button::Right);
          inputScript.push_back(assertSettingsNavigation(2, 1));
          inputScript.push_back(press(MappedInputManager::Button::Right));
          inputScript.push_back({ScriptActionType::WaitForSettingsCategory, down, nullptr, 0, 3, 0});
          inputScript.push_back(release(MappedInputManager::Button::Right));
          inputScript.push_back(assertSettingsNavigation(3, 1));
          inputScript.push_back(press(MappedInputManager::Button::Left));
          inputScript.push_back({ScriptActionType::WaitForSettingsCategory, down, nullptr, 0, 2, 0});
          inputScript.push_back(release(MappedInputManager::Button::Left));
          inputScript.push_back(assertSettingsNavigation(2, 1));
          addTap(MappedInputManager::Button::Left);  // Tab band.
          addTap(MappedInputManager::Button::Left);  // Wrap to Menu navigation.
          addTap(MappedInputManager::Button::Confirm);
          addTap(MappedInputManager::Button::Left);
          addTap(MappedInputManager::Button::Confirm);
          inputScript.push_back(render("Directional restored without moving the row", 3));
          inputScript.push_back(
              {ScriptActionType::AssertMenuNavigation, down, nullptr, 0, CrossPointSettings::MENU_NAV_DIRECTIONAL, 0});
          inputScript.push_back(assertSettingsNavigation(2, controlsCount));
          // Return to the Side Buttons submenu from the last row.
          const auto controls = buildControlsSettingsParentList(getSettingsList());
          const auto sideButtons = std::find_if(controls.begin(), controls.end(), [](const SettingInfo& setting) {
            return setting.action == SettingAction::ControlsSideButtons;
          });
          const int sideButtonsIndex = static_cast<int>(std::distance(controls.begin(), sideButtons));
          for (int i = sideButtonsIndex + 1; i < controlsCount; ++i) addTap(up);
          addTap(MappedInputManager::Button::Confirm);
          inputScript.push_back(render("Side Button Settings", 250));
          step = SmokeStep::ReaderInput;
          break;
        }
        [[fallthrough]];
      case SmokeStep::SideButtons:
        activityManager.replaceActivity(std::make_unique<EpubReaderDrawerActivity>(
            renderer, mappedInputManager, std::shared_ptr<Epub>{}, nullptr, 0.0f, 0, 0, false, false, false, false,
            false, false, false, false, true, true, 0, 0, 5, false));
        queueStep("Reader Menu", SmokeStep::ReaderMenu);
        break;

      case SmokeStep::ReaderMenu:
        activityManager.goToSleep();
        queueStep("Sleep", SmokeStep::Sleep);
        break;

      case SmokeStep::Sleep: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') {
          LOG_INF("SMOKE", "Skipping Reader step; CROSSINK_SIMULATOR_SMOKE_BOOK is not set");
          step = SmokeStep::Reader;
          break;
        }
        if (!Storage.exists(bookPath)) {
          fail("Smoke test book is missing: %s", bookPath);
        }
        if (landscapeReaderRequested()) {
          SETTINGS.orientation = CrossPointSettings::LANDSCAPE_CCW;
          LOG_INF("SMOKE", "Opening smoke reader in landscape");
        }
        activityManager.goToReader(bookPath, true);
        queueStep("Reader", SmokeStep::Reader, 8);
        break;
      }

      case SmokeStep::Reader:
        if (!activityManager.isCurrentActivityNamed("EpubReader")) fail("Completion test requires EPUB reader");
        if (!EpubReaderCompletionSmokeTest::run(
                *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity()))) {
          fail("EPUB completion shortcut regression");
        }
        queueStep("Completion shortcut returned Home", SmokeStep::CompletionReturnedHome);
        break;

      case SmokeStep::CompletionReturnedHome:
        if (!activityManager.isCurrentActivityNamed("Home")) fail("End-screen forward shortcut did not return Home");
        activityManager.goToReader(std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK"), true);
        queueStep("Reader after completion shortcuts", SmokeStep::CompletionReaderRestored, 8);
        break;

      case SmokeStep::CompletionReaderRestored:
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_STATUS_BAR_LIFECYCLE")) {
          if (!EpubReaderCompletionSmokeTest::openStatusSettings(
                  *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity())))
            fail("Global status size repaginated reader or missing initial chapter");
          queueStep("Reader status settings", SmokeStep::StatusBarReaderSettings, 4);
          break;
        }
        verifyWakePowerReaderShortcut();
        buildReaderInputScript();
        step = SmokeStep::ReaderInput;
        break;

      case SmokeStep::StatusBarReaderSettings: {
        if (!activityManager.isCurrentActivityNamed("StatusBarSettings")) fail("Reader status settings did not open");
        static constexpr unsigned masks[] = {3, 1, 2, 0, 3, 0};
        {
          RenderLock lock;
          auto top = SETTINGS.topReaderStatusBar;
          auto bottom = SETTINGS.bottomReaderStatusBar;
          top.hidden = masks[statusBarReaderPass] & 1;
          bottom.hidden = masks[statusBarReaderPass] & 2;
          SETTINGS.setReaderStatusBar(ReaderStatusBarPosition::Top, top);
          SETTINGS.setReaderStatusBar(ReaderStatusBarPosition::Bottom, bottom);
          SETTINGS.saveToFile();
        }
        inputScript.clear();
        scriptIndex = 0;
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Reader after status edit", 16));
        inputScript.push_back(assertActivity("EpubReader"));
        inputCompletionStep = SmokeStep::StatusBarReaderReturned;
        step = SmokeStep::ReaderInput;
        break;
      }
      case SmokeStep::StatusBarReaderReturned: {
        auto& reader = *static_cast<EpubReaderActivity*>(activityManager.simulatorCurrentActivity());
        if (!EpubReaderCompletionSmokeTest::statusSettingsReturned(reader)) fail("EPUB status reflow lost text anchor");
        {
          RenderLock lock;
          captureStatusBarScreen(("epub-status-return-" + std::to_string(statusBarReaderPass)).c_str());
        }
        if (++statusBarReaderPass < 6) {
          if (!EpubReaderCompletionSmokeTest::openStatusSettings(reader)) fail("Repeated reader status edit failed");
          queueStep("Repeated reader status settings", SmokeStep::StatusBarReaderSettings, 4);
        } else {
          LOG_INF("SMOKE", "EPUB repeated hide/show, actual Settings return and text-anchor reflow passed");
          buildReaderInputScript();
          step = SmokeStep::ReaderInput;
        }
        break;
      }

      case SmokeStep::ReaderInput:
        runReaderInputScript();
        break;

      case SmokeStep::CarouselHome:
        verifyCarouselCacheReturn();
        break;

      case SmokeStep::FrontlightLayout: {
        // Both scales, all four orientations, and all eight themes, with and
        // without the additional reader progress row.
        if (frontlightLayoutPass == 128) {
          LOG_INF("SMOKE", "Frontlight layout: 128 scale/orientation/theme/context combinations passed");
          step = SmokeStep::Done;
          break;
        }
        {
          RenderLock lock;
          SETTINGS.uiScale =
              (frontlightLayoutPass / 64) ? CrossPointSettings::UI_SCALE_LARGE : CrossPointSettings::UI_SCALE_SMALL;
          SETTINGS.uiTheme = (frontlightLayoutPass / 8) % 8;
          renderer.setOrientation(static_cast<GfxRenderer::Orientation>((frontlightLayoutPass / 2) % 4));
          UITheme::getInstance().reload();
        }
        FrontlightPanelContext context;
        context.activeReaderBook = frontlightLayoutPass % 2;
        if (context.activeReaderBook) {
          context.bookTitle = "Layout fixture";
          context.bookDetails.title = context.bookTitle;
          context.bookDetails.chapterPage = 1;
          context.bookDetails.chapterPageCount = 10;
        }
        activityManager.pushActivity(
            std::make_unique<FrontlightPanelActivity>(renderer, mappedInputManager, std::move(context)));
        queueStep("Frontlight layout matrix", SmokeStep::FrontlightLayoutRendered, 4);
        break;
      }

      case SmokeStep::FrontlightLayoutRendered: {
#if CROSSINK_APP_CAP_TOUCH
        auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity());
        if (!panel) fail("Layout matrix expected frontlight drawer");
        const auto handle = panel->simulatorHandleRect();
        int top, right, bottom, left;
        renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
        if (handle.y < top || handle.bottom() > renderer.getScreenHeight() - bottom || handle.x < left ||
            handle.right() > renderer.getScreenWidth() - right || handle.height <= 0 ||
            panel->simulatorContentBottom > panel->simulatorActionBarTop) {
          fail("Frontlight controls overflow in layout matrix case %u", frontlightLayoutPass);
        }
        if (const char* outputDir = std::getenv("CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_CAPTURES")) {
          const auto path = std::filesystem::path(outputDir) / (std::to_string(frontlightLayoutPass) + ".pgm");
          FILE* image = std::fopen(path.c_str(), "wb");
          if (!image) fail("Cannot create frontlight layout capture");
          RenderLock lock;
          const int width = renderer.getScreenWidth();
          const int height = renderer.getScreenHeight();
          std::fprintf(image, "P5\n%d %d\n255\n", width, height);
          for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
          }
          std::fclose(image);
        }
        const int x = handle.x + handle.width / 2;
        const int y = handle.y + handle.height / 2;
        inputScript = {touchDown(x, y), touchRelease(x, y), render("Frontlight closed by visible handle", 4),
                       assertActivity("Home")};
        scriptIndex = 0;
        ++frontlightLayoutPass;
        inputCompletionStep = SmokeStep::FrontlightLayout;
        step = SmokeStep::ReaderInput;
#else
        fail("Frontlight layout regression requires the X4 Pro simulator");
#endif
        break;
      }

      case SmokeStep::ThemeHome: {
#if CROSSINK_APP_CAP_TOUCH
        if (activityManager.getCurrentBookPath() != homeThemeBookPath) fail("Theme switch lost the selected book");
        const int width = renderer.getScreenWidth();
        const int height = renderer.getScreenHeight();
        inputScript = {touchDown(width / 2, 8),
                       touchMove(width / 2, height / 4),
                       touchRelease(width / 2, height / 4),
                       render("Home frontlight drawer", 4),
                       assertActivity("FrontlightPanel"),
                       {ScriptActionType::OpenFrontlightSettings, MappedInputManager::Button::Back, nullptr, 0, 0, 0},
                       render("Home drawer Settings", 4),
                       assertActivity("Settings")};
        scriptIndex = 0;
        inputCompletionStep = SmokeStep::ThemeSettings;
        step = SmokeStep::ReaderInput;
#else
        fail("Home theme regression requires the X4 Pro simulator");
#endif
        break;
      }

      case SmokeStep::ThemeSettings: {
        static constexpr uint8_t themes[] = {CrossPointSettings::LYRA_3_COVERS, CrossPointSettings::DASHBOARD,
                                             CrossPointSettings::MINIMAL,       CrossPointSettings::COVER_GRID,
                                             CrossPointSettings::LYRA,          CrossPointSettings::CLASSIC,
                                             CrossPointSettings::ROUNDEDRAFF,   CrossPointSettings::LYRA_CAROUSEL,
                                             CrossPointSettings::LYRA_CAROUSEL, CrossPointSettings::LYRA_CAROUSEL};
        // Change the global values while the real Settings child is open, then
        // return through its real drawer callback. Compare with a fresh Home.
        {
          RenderLock lock;
          SETTINGS.uiTheme = themes[homeThemePass];
          if (homeThemePass == 8) SETTINGS.uiScale = CrossPointSettings::UI_SCALE_LARGE;
          if (homeThemePass == 9) SETTINGS.displayStatusBarTextSize = 2;
          UITheme::getInstance().reload();
        }
        inputScript = {press(MappedInputManager::Button::Back), release(MappedInputManager::Button::Back),
                       render("Home after drawer Settings", 8), assertActivity("Home")};
        scriptIndex = 0;
        inputCompletionStep = SmokeStep::ThemeReturned;
        step = SmokeStep::ReaderInput;
        break;
      }

      case SmokeStep::ThemeReturned: {
        if (activityManager.getCurrentBookPath() != homeThemeBookPath) fail("Theme switch lost the selected book");
        {
          RenderLock lock;
          homeThemeScreenHash = hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize());
          captureStatusBarScreen(("home-theme-size-pass-" + std::to_string(homeThemePass)).c_str());
        }
        activityManager.replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInputManager, HomeMenuItem::NONE,
                                                                       HalDisplay::FAST_REFRESH, homeThemeBookPath));
        queueStep("Fresh Home reference", SmokeStep::ThemeFresh, 8);
        break;
      }

      case SmokeStep::ThemeFresh: {
        {
          RenderLock lock;
          if (hashBytes(renderer.getFrameBuffer(), renderer.getBufferSize()) != homeThemeScreenHash)
            fail("Home after drawer theme change differs from fresh Home (pass %u)", homeThemePass);
        }
        LOG_INF("SMOKE", "Home theme/scale/status-size return matches fresh render (pass %u)", homeThemePass);
        if (++homeThemePass == 10) {
          LOG_INF("SMOKE", "Simulator smoke test passed");
          std::_Exit(0);
        }
        step = SmokeStep::ThemeHome;
        break;
      }

      case SmokeStep::Done:
        if (std::getenv("CROSSINK_SIMULATOR_SMOKE_APP_NOTES")) {
          RenderLock lock;
          captureStatusBarScreen("short-note-popup");
        }
        if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL && carouselCachePass == 0 &&
            std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK")) {
          const RecentBook book = RECENT_BOOKS.getBooks().front();
          for (const char* path : {"/books/carousel-second.txt", "/books/carousel-third.txt"}) {
            if (!Storage.writeFile(path, "Carousel side cover fixture")) fail("Cannot create carousel fixture");
            RECENT_BOOKS.addOrUpdateBook(path, path, {}, {}, RecentBook::CoverState::Missing);
          }
          RECENT_BOOKS.addOrUpdateBook(book.path, book.title, book.author, book.coverBmpPath, book.coverState);
          SETTINGS.trackReadingStats = 1;
          activityManager.goHome();
          queueStep("Carousel return cache", SmokeStep::CarouselHome, 8);
          break;
        }
        LOG_INF("SMOKE", "Simulator smoke test passed");
        std::_Exit(0);
    }
  }

  static ScriptAction assertReaderMenu(const ReaderDrawerTab tab, const ReaderDrawerPane pane, const int selected) {
    return {ScriptActionType::AssertReaderMenu,
            MappedInputManager::Button::Back,
            nullptr,
            selected,
            static_cast<int>(tab),
            static_cast<int>(pane)};
  }

  static ScriptAction assertSettingsNavigation(const int category, const int selected) {
    return {
        ScriptActionType::AssertSettingsNavigation, MappedInputManager::Button::Back, nullptr, 0, category, selected};
  }

  static ScriptAction press(MappedInputManager::Button button) {
    return {ScriptActionType::Press, button, nullptr, 0, 0, 0};
  }

  static ScriptAction release(MappedInputManager::Button button) {
    return {ScriptActionType::Release, button, nullptr, 0, 0, 0};
  }

  static ScriptAction homeTap() {
    return {ScriptActionType::HomeTap, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction homeLongPress() {
    return {ScriptActionType::HomeLongPress, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction configureHomeButtonPowerLock() {
    return {ScriptActionType::ConfigureHomeButtonPowerLock, MappedInputManager::Button::Power, nullptr, 0, 0, 0};
  }

  static ScriptAction waitForPowerLongPress() {
    return {ScriptActionType::WaitForPowerLongPress, MappedInputManager::Button::Power, nullptr, 0, 0, 0};
  }

  static ScriptAction assertHomeButtonDisabled() {
    return {ScriptActionType::AssertHomeButtonDisabled, MappedInputManager::Button::Power, nullptr, 0, 0, 0};
  }

  static ScriptAction assertHomeButtonEnabled() {
    return {ScriptActionType::AssertHomeButtonEnabled, MappedInputManager::Button::Power, nullptr, 0, 0, 0};
  }

  static ScriptAction assertTouchscreenDisabled() {
    return {ScriptActionType::AssertTouchscreenDisabled, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction assertTouchscreenEnabled() {
    return {ScriptActionType::AssertTouchscreenEnabled, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction assertTtfProfileNative() {
    return {ScriptActionType::AssertTtfProfileNative, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction openSmokeBook() {
    return {ScriptActionType::OpenSmokeBook, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction disableReaderTouch() {
    return {ScriptActionType::DisableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction enableReaderTouch() {
    return {ScriptActionType::EnableReaderTouch, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }

  static ScriptAction render(const char* label, int framesToSettle = 3) {
    return {ScriptActionType::Render, MappedInputManager::Button::Back, label, framesToSettle, 0, 0};
  }

  static ScriptAction assertActivity(const char* name) {
    return {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, name, 0, 0, 0};
  }

#if CROSSINK_APP_CAP_TOUCH
  static ScriptAction touchDown(const int x, const int y) {
    return {ScriptActionType::TouchDown, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchDrawerHandle() {
    return {ScriptActionType::TouchDrawerHandle, MappedInputManager::Button::Back, nullptr, 0, 0, 0};
  }
  static ScriptAction touchFrontlightQuickAction(const int index) {
    return {ScriptActionType::TouchFrontlightQuickAction, MappedInputManager::Button::Back, nullptr, 0, index, 0};
  }
  static ScriptAction touchMove(const int x, const int y) {
    return {ScriptActionType::TouchMove, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
  static ScriptAction touchRelease(const int x, const int y) {
    return {ScriptActionType::TouchRelease, MappedInputManager::Button::Back, nullptr, 0, x, y};
  }
#endif

  void addTap(MappedInputManager::Button button) {
    inputScript.push_back(press(button));
    inputScript.push_back(release(button));
  }

  unsigned statusBarReaderPass = 0;

  void buildReaderInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputCompletionStep = SmokeStep::Done;

    int currentFontIndex = SETTINGS.fontFamily;
    std::vector<uint8_t> currentFontSizes(std::begin(BUILTIN_READER_FONT_SIZES), std::end(BUILTIN_READER_FONT_SIZES));
    if (SETTINGS.sdFontFamilyName[0] != '\0') {
      sdFontSystem.ensureRegistry();
      const auto& families = sdFontSystem.registry().getFamilies();
      for (size_t i = 0; i < families.size(); ++i) {
        if (families[i].name != SETTINGS.sdFontFamilyName) continue;
        currentFontIndex = CrossPointSettings::BUILTIN_FONT_COUNT + static_cast<int>(i);
        currentFontSizes = families[i].availableSizes();
        break;
      }
    }
    const auto sizeIt = std::find(currentFontSizes.begin(), currentFontSizes.end(), SETTINGS.readerFontPointSize);
    const int currentSizeIndex = sizeIt == currentFontSizes.end() ? 0 : std::distance(currentFontSizes.begin(), sizeIt);
    inputScript.push_back(
        {ScriptActionType::ConfigureChapterShortcuts, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
    addTap(MappedInputManager::Button::Power);
    inputScript.push_back(render("Select Chapter from short Power", 4));
    inputScript.push_back(
        {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, "EpubReaderChapterSelection", 0, 0, 0});
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after cancelling chapter selection", 4));
    inputScript.push_back(press(MappedInputManager::Button::Power));
    inputScript.push_back(waitForPowerLongPress());
    inputScript.push_back(render("Select Chapter from long Power", 4));
    inputScript.push_back(release(MappedInputManager::Button::Power));
    inputScript.push_back(render("Chapter selection after Power release", 4));
    inputScript.push_back(
        {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, "EpubReaderChapterSelection", 0, 0, 0});
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after long Power chapter shortcut", 4));
    inputScript.push_back(press(MappedInputManager::Button::Confirm));
    inputScript.push_back(
        {ScriptActionType::WaitForMenuLongPress, MappedInputManager::Button::Confirm, nullptr, 0, 0, 0});
    inputScript.push_back(render("Select Chapter from long Menu", 4));
    inputScript.push_back(release(MappedInputManager::Button::Confirm));
    inputScript.push_back(render("Chapter selection after Menu release", 4));
    inputScript.push_back(
        {ScriptActionType::AssertActivity, MappedInputManager::Button::Back, "EpubReaderChapterSelection", 0, 0, 0});
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after long Menu chapter shortcut", 4));
    if (mappedInputManager.hasHomeKey()) {
      for (const auto action : {ScriptActionType::HomeTap, ScriptActionType::HomeLongPress}) {
        inputScript.push_back({action, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
        inputScript.push_back(
            {ScriptActionType::WaitForChapterSelection, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
        inputScript.push_back(render("Select Chapter from Home key", 8));
        inputScript.push_back(assertActivity("EpubReaderChapterSelection"));
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Reader after Home chapter shortcut", 4));
        inputScript.push_back(assertActivity("EpubReader"));
      }
      inputScript.push_back(
          {ScriptActionType::ConfigureChapterHomeDoubleTap, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
      inputScript.push_back({ScriptActionType::HomeTap, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
      inputScript.push_back({ScriptActionType::HomeTap, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
      inputScript.push_back(
          {ScriptActionType::WaitForChapterSelection, MappedInputManager::Button::Power, nullptr, 0, 0, 0});
      inputScript.push_back(render("Select Chapter from Home double tap", 4));
      inputScript.push_back(assertActivity("EpubReaderChapterSelection"));
      addTap(MappedInputManager::Button::Back);
      inputScript.push_back(render("Reader after Home double tap chapter shortcut", 4));
      inputScript.push_back(assertActivity("EpubReader"));
    }
    inputScript.push_back(
        {ScriptActionType::RestoreChapterShortcuts, MappedInputManager::Button::Power, nullptr, 0, 0, 0});

    const int turns = pageTurnCount();
#if CROSSINK_APP_CAP_TOUCH
    if (mappedInputManager.hasTouch()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      if (width <= 0 || height <= 0) fail("Touch smoke test has invalid screen dimensions");
      LOG_INF("SMOKE", "Running touch reader input script with %d page turn(s)", turns);
      const int tabY = height - 28;
      for (int i = 0; i < turns; ++i) {
        inputScript.push_back(touchDown(width * 5 / 6, height / 2));
        inputScript.push_back(touchRelease(width * 5 / 6, height / 2));
        inputScript.push_back(render("Reader after touch page forward", 4));
      }

      // Exercise the TTF edit path that replaces the active scalable font IDs:
      // Auto -> Native, switch tabs, then return to the current page.
#if CROSSINK_SCALABLE_FONTS
      if (SETTINGS.sdFontFamilyName[0] != '\0' && sdFontSystem.isScalableFamily(SETTINGS.sdFontFamilyName)) {
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader Menu opened for TTF Native transition", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(touchDown(width / (static_cast<int>(READER_DRAWER_TAB_COUNT) * 2), tabY));
        inputScript.push_back(touchRelease(width / (static_cast<int>(READER_DRAWER_TAB_COUNT) * 2), tabY));
        addTap(MappedInputManager::Button::Confirm);
        addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("TTF Rendering opened in reader drawer", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("TTF Hinting choices opened in reader drawer", 3));
        const int currentHinting = TTF_RENDER_PROFILES.profileFor(SETTINGS.sdFontFamilyName).hinting;
        inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::EnumOptions, currentHinting));
        const auto hintingDirection = mappedInputManager.menuButton(
            currentHinting > 1 ? MappedInputManager::Button::Up : MappedInputManager::Button::Down);
        for (int i = 0; i < std::abs(currentHinting - 1); ++i) addTap(hintingDirection);
        addTap(MappedInputManager::Button::Confirm);
        inputScript.push_back(render("TTF Native hinting selected", 4));
        inputScript.push_back(assertTtfProfileNative());
        inputScript.push_back(touchDown(width / 2, tabY));
        inputScript.push_back(touchRelease(width / 2, tabY));
        inputScript.push_back(render("Reader Menu tab changed after TTF Native selection", 5));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        addTap(MappedInputManager::Button::Back);
        inputScript.push_back(render("Reader restored after TTF Native selection", 10));
        inputScript.push_back(assertActivity("EpubReader"));
        addTap(MappedInputManager::Button::PageForward);
        inputScript.push_back(render("Reader page turn after TTF Native selection", 5));
      }
#endif

      if (mappedInputManager.hasHomeKey()) {
        // Reader long-Power actions fire at the hold threshold. Their release
        // must not reach main.cpp's global shortcut route and run the same
        // action again. Repeat the gesture to verify the consumed release does
        // not leave the next one latched.
        inputScript.push_back(configureHomeButtonPowerLock());
        inputScript.push_back(press(MappedInputManager::Button::Power));
        inputScript.push_back(waitForPowerLongPress());
        inputScript.push_back(assertHomeButtonDisabled());
        inputScript.push_back(release(MappedInputManager::Button::Power));
        inputScript.push_back(assertHomeButtonDisabled());
        inputScript.push_back(press(MappedInputManager::Button::Power));
        inputScript.push_back(waitForPowerLongPress());
        inputScript.push_back(assertHomeButtonEnabled());
        inputScript.push_back(release(MappedInputManager::Button::Power));
        inputScript.push_back(assertHomeButtonEnabled());

        // X4 Pro reserves the top-edge swipe for its frontlight overlay and
        // moves the reader menu to the bottom edge.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
        inputScript.push_back(touchDown(header.x + header.width - 32, header.y + header.height / 2));
        inputScript.push_back(touchRelease(header.x + header.width - 32, header.y + header.height / 2));
        inputScript.push_back(render("Home opened by Frontlight Panel Home button", 4));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after Frontlight Panel Home button", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened after Home button", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(20, height / 3));
        inputScript.push_back(touchMove(20, 8));
        inputScript.push_back(touchRelease(20, 8));
        inputScript.push_back(render("Frontlight Panel remains open after in-drawer swipe up", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        // Use the rendered handle: its position follows the global status text size.
        inputScript.push_back(touchDrawerHandle());
        inputScript.push_back(touchMove(width / 2, 8));
        inputScript.push_back(touchRelease(width / 2, 8));
        inputScript.push_back(render("Reader restored after Frontlight Panel handle drag up", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        // The fourth action-bar slot opens Global Settings through the real
        // FrontlightPanelActivity callback path.
        inputScript.push_back(touchFrontlightQuickAction(3));
        inputScript.push_back(render("Global Settings opened from Frontlight Panel", 4));
        inputScript.push_back(assertActivity("Settings"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchMove(width / 2, height / 2));
        inputScript.push_back(touchRelease(width / 2, height / 2));
        inputScript.push_back(render("Global Settings remains open after interior swipe up", 4));
        inputScript.push_back(assertActivity("Settings"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after Settings bottom-edge swipe", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight Panel reopened after Global Settings", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchFrontlightQuickAction(1));
        inputScript.push_back(render("Sync dialog opened from Frontlight Panel", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height - 60));
        inputScript.push_back(touchRelease(width / 2, height - 60));
        inputScript.push_back(render("Reader restored after dismissing Frontlight sync dialog", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(touchDown(width / 2, height / 2 + 24));
        inputScript.push_back(touchRelease(width / 2, height / 2 + 24));
        inputScript.push_back(render("Reader Font opened from touch reader menu", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Reader Menu root restored by simulated Home key tap", 8));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Reader restored by simulated Home key tap at drawer root", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu reopened from simulated Home key hold", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchMove(width / 2, height - 8));
        inputScript.push_back(touchRelease(width / 2, height - 8));
        inputScript.push_back(render("Reader Menu remains open after in-drawer swipe down", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(touchDrawerHandle());
        inputScript.push_back(touchMove(width / 2, height - 8));
        inputScript.push_back(touchRelease(width / 2, height - 8));
        inputScript.push_back(render("Reader restored after Reader Menu handle drag down", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(disableReaderTouch());
        inputScript.push_back(homeLongPress());
        inputScript.push_back(render("Reader Menu opened from Home key hold with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
        inputScript.push_back(touchDown(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Reader restored after Home key menu with touch disabled", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(homeTap());
        inputScript.push_back(render("Home opened from simulated Home key tap", 8));
        inputScript.push_back(assertActivity("Home"));
        inputScript.push_back(enableReaderTouch());
        inputScript.push_back(openSmokeBook());
        inputScript.push_back(render("Reader reopened after simulated Home key tap", 8));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      } else {
        // Sticky uses the same vertical gesture split as X4 Pro: swipe down
        // opens reader details/actions and swipe up opens the bottom menu.
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Sticky Reader Details opened from touch gesture", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(20, height / 3));
        inputScript.push_back(touchMove(20, 8));
        inputScript.push_back(touchRelease(20, 8));
        inputScript.push_back(render("Sticky Reader Details remains open after in-drawer swipe up", 4));
        inputScript.push_back(assertActivity("FrontlightPanel"));
        inputScript.push_back(touchDown(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
        inputScript.push_back(render("Reader restored after Sticky details outside tap", 4));
        inputScript.push_back(assertActivity("EpubReader"));
        inputScript.push_back(touchDown(width / 2, height - 8));
        inputScript.push_back(touchMove(width / 2, height * 3 / 4));
        inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      }
      inputScript.push_back(render("Reader Menu opened from touch gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));

      const int fontTabX = width / (static_cast<int>(READER_DRAWER_TAB_COUNT) * 2);
      inputScript.push_back(touchDown(fontTabX, tabY));
      inputScript.push_back(touchRelease(fontTabX, tabY));
      addTap(MappedInputManager::Button::Confirm);
      inputScript.push_back(render("Touch Reader Font choices", 3));
      addTap(MappedInputManager::Button::Confirm);
      inputScript.push_back(render("Touch Font Family opens on current choice", 4));
      inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::FontFamily, currentFontIndex));
      addTap(MappedInputManager::Button::Back);
      addTap(mappedInputManager.menuButton(MappedInputManager::Button::Down));
      addTap(MappedInputManager::Button::Confirm);
      inputScript.push_back(render("Touch Font Size opens on current choice", 4));
      inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::EnumOptions, currentSizeIndex));
      addTap(MappedInputManager::Button::Back);
      addTap(MappedInputManager::Button::Back);

      // Touch every bottom-drawer tab slot, then dismiss from its handle.
      for (int tab = 0; tab < static_cast<int>(READER_DRAWER_TAB_COUNT); ++tab) {
        const int tabX = width * (tab * 2 + 1) / (static_cast<int>(READER_DRAWER_TAB_COUNT) * 2);
        inputScript.push_back(touchDown(tabX, tabY));
        inputScript.push_back(touchRelease(tabX, tabY));
        inputScript.push_back(render("Touch Reader Menu tab", 3));
        inputScript.push_back(assertActivity("EpubReaderDrawer"));
      }

      const int moreTabX = width / 2;
      const int drawerTop = height / 2;
      constexpr int rootRowStep = 60;
      constexpr int rootRowCenterOffset = 31;
      inputScript.push_back(touchDown(moreTabX, tabY));
      inputScript.push_back(touchRelease(moreTabX, tabY));
      inputScript.push_back(render("Touch Reader Menu More tab", 3));
      inputScript.push_back(touchDown(width / 2, drawerTop + rootRowStep + rootRowCenterOffset));
      inputScript.push_back(touchRelease(width / 2, drawerTop + rootRowStep + rootRowCenterOffset));
      inputScript.push_back(render("Touch Reader Go to Percent pane", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));
      inputScript.push_back(touchDown(20, drawerTop + 26));
      inputScript.push_back(touchRelease(20, drawerTop + 26));
      inputScript.push_back(render("Touch Reader More tab restored", 3));
      inputScript.push_back(touchDown(width / 2, drawerTop + rootRowStep * 2 + rootRowCenterOffset));
      inputScript.push_back(touchRelease(width / 2, drawerTop + rootRowStep * 2 + rootRowCenterOffset));
      inputScript.push_back(render("Touch Reader Auto Page Turn pane", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));
      inputScript.push_back(touchDown(20, drawerTop + 26));
      inputScript.push_back(touchRelease(20, drawerTop + 26));
      inputScript.push_back(render("Touch Reader More tab restored", 3));
      inputScript.push_back(touchDown(width / 2, height * 3 / 4));
      inputScript.push_back(touchMove(width / 2, height - 8));
      inputScript.push_back(touchRelease(width / 2, height - 8));
      inputScript.push_back(render("Reader Menu remains open after in-drawer swipe down", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));
      inputScript.push_back(touchDown(width / 2, drawerTop - 14));
      inputScript.push_back(touchRelease(width / 2, drawerTop - 14));
      inputScript.push_back(render("Reader restored after drawer handle tap", 4));
      inputScript.push_back(assertActivity("EpubReader"));

      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Reader Menu reopened for bottom-edge Home gesture", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));
      inputScript.push_back(touchDown(width / 2, height * 3 / 4));
      inputScript.push_back(touchMove(width / 2, height / 2 + 8));
      inputScript.push_back(touchRelease(width / 2, height / 2 + 8));
      inputScript.push_back(render("Reader Menu remains open after interior swipe up", 4));
      inputScript.push_back(assertActivity("EpubReaderDrawer"));
      inputScript.push_back(touchDown(width / 2, height - 8));
      inputScript.push_back(touchMove(width / 2, height * 3 / 4));
      inputScript.push_back(touchRelease(width / 2, height * 3 / 4));
      inputScript.push_back(render("Home opened from Reader Menu bottom-edge swipe", 6));
      inputScript.push_back(assertActivity("Home"));
      return;
    }
#endif
    const auto menuDown = mappedInputManager.menuButton(MappedInputManager::Button::Down);
    const auto menuLeft = mappedInputManager.menuButton(MappedInputManager::Button::Left);
    const auto menuRight = mappedInputManager.menuButton(MappedInputManager::Button::Right);
    for (int i = 0; i < turns; i++) {
      addTap(MappedInputManager::Button::PageForward);
      inputScript.push_back(render("Reader after page forward", 4));
    }

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Menu opened from EPUB", 4));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    // Change the shared setting through the real in-reader Controls screen.
    addTap(MappedInputManager::Button::Confirm);  // Location
    addTap(MappedInputManager::Button::Confirm);  // Settings
    addTap(menuDown);                             // Status Bar
    addTap(menuDown);                             // Controls
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("In-reader Controls opened", 5));
    inputScript.push_back(assertActivity("ControlsOptions"));
    const int controlsCount = static_cast<int>(buildControlsSettingsParentList(getSettingsList()).size());
    for (int i = 1; i < controlsCount; ++i) addTap(menuDown);
    inputScript.push_back(
        {ScriptActionType::CaptureMenuNavigation, menuDown, "menu-navigation-reader-controls", 0, 0, 0});
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Right);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(
        {ScriptActionType::AssertMenuNavigation, menuDown, nullptr, 0, CrossPointSettings::MENU_NAV_CLASSIC, 0});
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader menu after changing Controls", 5));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Settings, ReaderDrawerPane::Root, 1));
    for (int i = 0; i < 3; ++i) addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Right);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 1));
    addTap(MappedInputManager::Button::Left);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    inputScript.push_back(render("Classic reader menu row navigation", 3));
    inputScript.push_back(
        {ScriptActionType::CaptureMenuNavigation, menuDown, "menu-navigation-reader-classic", 0, 0, 0});
    addTap(MappedInputManager::Button::Back);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Location, ReaderDrawerPane::Root, 0));
    // Restore the starting tab via Select, the classic tab-band control.
    for (int i = 0; i < 4; ++i) addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    addTap(MappedInputManager::Button::Confirm);  // Location
    addTap(MappedInputManager::Button::Confirm);  // Settings
    addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("In-reader Controls reopened in Classic", 5));
    inputScript.push_back(assertActivity("ControlsOptions"));
    for (int i = 1; i < controlsCount; ++i) addTap(MappedInputManager::Button::Down);
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Left);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(
        {ScriptActionType::AssertMenuNavigation, menuDown, nullptr, 0, CrossPointSettings::MENU_NAV_DIRECTIONAL, 0});
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader menu after restoring Directional", 5));
    for (int i = 0; i < 3; ++i) addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    // Select retains its original tab-band behavior, including wraparound.
    for (const auto tab : {ReaderDrawerTab::Location, ReaderDrawerTab::Settings, ReaderDrawerTab::Font,
                           ReaderDrawerTab::Layout, ReaderDrawerTab::More}) {
      addTap(MappedInputManager::Button::Confirm);
      inputScript.push_back(render("Reader Menu Select advances tab", 3));
      inputScript.push_back(assertReaderMenu(tab, ReaderDrawerPane::Root, 0));
    }
    addTap(menuLeft);
    addTap(menuLeft);
    inputScript.push_back(render("Reader Menu Font tab reached with Left", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::Root, 0));
    addTap(menuLeft);
    inputScript.push_back(render("Reader Menu wraps left to Settings", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Settings, ReaderDrawerPane::Root, 0));
    addTap(menuRight);
    inputScript.push_back(render("Reader Menu wraps right to Font", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::Root, 0));

    addTap(menuDown);
    inputScript.push_back(render("Reader Menu first row focused", 3));
    addTap(menuDown);
    inputScript.push_back(render("Reader Menu Down moves row focus", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::Root, 1));
    addTap(menuRight);
    inputScript.push_back(render("Reader Menu Right switches tab while a row is focused", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Layout, ReaderDrawerPane::Root, 0));
    addTap(menuLeft);
    addTap(menuDown);
    inputScript.push_back(render("Reader Menu first Font row focused again", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::Root, 0));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Font opened from Reader Menu", 4));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::ReaderFont, 0));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Font Family opens on current choice", 4));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::FontFamily, currentFontIndex));
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Font after closing Font Family", 3));

    addTap(menuDown);
    inputScript.push_back(render("Font Size selected", 3));

    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Font Size choices opened", 3));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Font, ReaderDrawerPane::EnumOptions, currentSizeIndex));

#if CROSSINK_APP_READER_SAMPLE_PREVIEW
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("C3 font size paragraph preview", 4));
#endif

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Font after closing Font Size", 4));

#if CROSSINK_APP_READER_SAMPLE_PREVIEW
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("C3 font family picker", 4));
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("C3 font family paragraph preview", 4));
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("C3 reader font restored", 3));
#endif

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader Menu tab focus restored", 4));

#if CROSSINK_APP_READER_SAMPLE_PREVIEW
    addTap(menuDown);
    addTap(menuDown);
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("C3 spacing paragraph preview", 4));
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("C3 line spacing adjusted", 4));
    addTap(MappedInputManager::Button::Back);
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("C3 word spacing adjusted", 4));
    addTap(MappedInputManager::Button::Back);
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("C3 font tab restored", 3));
#endif

    addTap(menuRight);
    inputScript.push_back(render("Reader Menu advanced to next tab", 4));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::Layout, ReaderDrawerPane::Root, 0));

#if CROSSINK_APP_READER_SAMPLE_PREVIEW
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("C3 margin paragraph preview", 4));
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("C3 vertical margin adjusted", 4));
    addTap(MappedInputManager::Button::Back);
    addTap(menuDown);
    addTap(MappedInputManager::Button::Confirm);
    addTap(MappedInputManager::Button::Down);
    inputScript.push_back(render("C3 horizontal margin adjusted", 4));
    addTap(MappedInputManager::Button::Back);
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("C3 layout tab restored", 3));
#endif

    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader after closing Reader Menu", 4));
    inputScript.push_back(assertActivity("EpubReader"));
    addTap(MappedInputManager::Button::Confirm);
    inputScript.push_back(render("Reader Menu reopened on More", 4));
    inputScript.push_back(assertReaderMenu(ReaderDrawerTab::More, ReaderDrawerPane::Root, 0));
    addTap(MappedInputManager::Button::Back);
    inputScript.push_back(render("Reader restored after checking default tab", 4));

    LOG_INF("SMOKE", "Running reader input script with %d page turn(s)", turns);
  }

#if CROSSINK_APP_CAP_TOUCH
  void buildFileBrowserInputScript() {
    inputScript.clear();
    scriptIndex = 0;
    inputCompletionStep = SmokeStep::FileBrowserSettings;

    if (mappedInputManager.hasHomeKey()) {
      const int width = renderer.getScreenWidth();
      const int height = renderer.getScreenHeight();
      inputScript.push_back(touchDown(width / 2, 8));
      inputScript.push_back(touchMove(width / 2, height / 4));
      inputScript.push_back(touchRelease(width / 2, height / 4));
      inputScript.push_back(render("Frontlight Panel opened outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width * 9 / 10, height / 2));
      inputScript.push_back(touchRelease(width * 9 / 10, height / 2));
      inputScript.push_back(render("Reader touchscreen disabled from Frontlight Panel outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width / 2, height - 60));
      inputScript.push_back(touchRelease(width / 2, height - 60));
      inputScript.push_back(render("File Browser restored after disabling reader touchscreen", 4));
      inputScript.push_back(assertActivity("FileBrowser"));
      inputScript.push_back(assertTouchscreenDisabled());
      inputScript.push_back(touchDown(width / 2, 8));
      inputScript.push_back(touchMove(width / 2, height / 4));
      inputScript.push_back(touchRelease(width / 2, height / 4));
      inputScript.push_back(render("Frontlight Panel reopened outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width * 9 / 10, height / 2));
      inputScript.push_back(touchRelease(width * 9 / 10, height / 2));
      inputScript.push_back(render("Reader touchscreen enabled from Frontlight Panel outside Reader", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(touchDown(width / 2, height - 60));
      inputScript.push_back(touchRelease(width / 2, height - 60));
      inputScript.push_back(render("File Browser restored after enabling reader touchscreen", 4));
      inputScript.push_back(assertActivity("FileBrowser"));
      inputScript.push_back(assertTouchscreenEnabled());
      inputScript.push_back(
          {ScriptActionType::ClearFrontlightSyncBook, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
      inputScript.push_back(touchDown(width / 2, 8));
      inputScript.push_back(touchMove(width / 2, height / 4));
      inputScript.push_back(touchRelease(width / 2, height / 4));
      inputScript.push_back(render("Frontlight without last-read book", 4));
      inputScript.push_back({ScriptActionType::OpenFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
      inputScript.push_back(render("Unavailable transfer actions rendered", 4));
      inputScript.push_back(
          {ScriptActionType::CheckFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 1, 0});
      inputScript.push_back(press(MappedInputManager::Button::Confirm));
      inputScript.push_back(release(MappedInputManager::Button::Confirm));
      inputScript.push_back(render("Unavailable transfer action cannot activate", 4));
      inputScript.push_back(assertActivity("FrontlightPanel"));
      inputScript.push_back(press(MappedInputManager::Button::Back));
      inputScript.push_back(release(MappedInputManager::Button::Back));
      inputScript.push_back(render("File Browser restored after unavailable transfer", 4));
      inputScript.push_back(assertActivity("FileBrowser"));
      inputScript.push_back(
          {ScriptActionType::PrepareFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
      for (const bool statsEnabled : {true, false}) {
        inputScript.push_back(
            {statsEnabled ? ScriptActionType::EnableReadingStats : ScriptActionType::DisableReadingStats,
             MappedInputManager::Button::Back, nullptr, 0, 0, 0});
        inputScript.push_back(touchDown(width / 2, 8));
        inputScript.push_back(touchMove(width / 2, height / 4));
        inputScript.push_back(touchRelease(width / 2, height / 4));
        inputScript.push_back(render("Frontlight sync from File Browser", 4));
        inputScript.push_back(
            {ScriptActionType::OpenFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
        inputScript.push_back(render("Frontlight sync menu rendered", 4));
        inputScript.push_back(
            {ScriptActionType::CheckFrontlightSync, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
        inputScript.push_back(press(MappedInputManager::Button::Confirm));
        inputScript.push_back(release(MappedInputManager::Button::Confirm));
        inputScript.push_back(render("Frontlight sync opens account settings", 4));
        inputScript.push_back(assertActivity("KOReaderSettings"));
        inputScript.push_back(press(MappedInputManager::Button::Back));
        inputScript.push_back(release(MappedInputManager::Button::Back));
        inputScript.push_back(render("File Browser restored after sync settings", 4));
        inputScript.push_back(assertActivity("FileBrowser"));
      }
      inputScript.push_back({ScriptActionType::EnableReadingStats, MappedInputManager::Button::Back, nullptr, 0, 0, 0});
    }

    const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInputManager);
    const auto backLayout = TouchHeaderBackButton::layout(header);
    const int x = header.x + header.width - backLayout.iconRect.width / 2;
    const int y = backLayout.iconRect.y + backLayout.iconRect.height / 2;
    inputScript.push_back(touchDown(x, y));
    inputScript.push_back(touchRelease(x, y));
    inputScript.push_back(render("File Browser Settings opened from header shortcut", 4));
    inputScript.push_back(assertActivity("FileBrowserSettings"));
    const int rowY = header.y + header.height + 32;
    inputScript.push_back(touchDown(renderer.getScreenWidth() / 2, rowY));
    inputScript.push_back(touchRelease(renderer.getScreenWidth() / 2, rowY));
    inputScript.push_back(render("File Browser Settings toggle without row highlight", 4));
    inputScript.push_back(assertActivity("FileBrowserSettings"));
  }
#endif

  void runReaderInputScript() {
    if (scriptIndex >= inputScript.size()) {
      step = inputCompletionStep;
      return;
    }

    const auto& action = inputScript[scriptIndex++];
    switch (action.type) {
      case ScriptActionType::ClearFrontlightSyncBook:
        APP_STATE.openEpubPath.clear();
        break;
      case ScriptActionType::PrepareFrontlightSync: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (!bookPath || !Storage.exists(bookPath)) fail("Frontlight sync fixture is missing");
        APP_STATE.openEpubPath = bookPath;
        break;
      }
      case ScriptActionType::DisableReadingStats:
        SETTINGS.trackReadingStats = 0;
        break;
      case ScriptActionType::EnableReadingStats:
        SETTINGS.trackReadingStats = 1;
        break;
      case ScriptActionType::OpenFrontlightSync: {
        auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity());
        if (!panel) fail("Expected frontlight drawer before opening sync menu");
        panel->simulatorActivateQuickAction(1);
        break;
      }
      case ScriptActionType::CheckFrontlightSync: {
        auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity());
        if (!panel) fail("Expected frontlight sync menu");
        const bool unavailable = action.x != 0;
        for (int index = 0; index < 3; ++index) {
          if (panel->simulatorSyncOptionDisabled(index) != unavailable)
            fail("Unexpected transfer availability outside reader");
          const auto row = panel->simulatorSyncOptionRect(index);
          // Sample inside the row background, away from its centered label and rounded corners.
          if (unavailable || index > 0) {
            for (int offset = 0; offset < 8; ++offset)
              if (renderer.isPixelBlack(row.x + row.width / 2 + offset, row.y + 4))
                fail("Unselected sync option is highlighted");
          }
        }
        if (const char* outputDir = std::getenv("CROSSINK_SIMULATOR_SMOKE_FRONTLIGHT_CAPTURES")) {
          const auto path = std::filesystem::path(outputDir) / (unavailable                  ? "sync-no-book.pgm"
                                                                : SETTINGS.trackReadingStats ? "sync-stats-on.pgm"
                                                                                             : "sync-stats-off.pgm");
          FILE* image = std::fopen(path.c_str(), "wb");
          if (!image) fail("Cannot create sync menu capture");
          RenderLock lock;
          const int width = renderer.getScreenWidth();
          const int height = renderer.getScreenHeight();
          std::fprintf(image, "P5\n%d %d\n255\n", width, height);
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) std::fputc(renderer.isPixelBlack(x, y) ? 0 : 255, image);
          std::fclose(image);
        }
        break;
      }
      case ScriptActionType::OpenFrontlightSettings: {
        auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity());
        if (!panel) fail("Expected frontlight drawer before opening Settings");
        panel->simulatorActivateQuickAction(3);
        break;
      }
      case ScriptActionType::Press:
        mappedInputManager.simulatorInjectPress(action.button);
        break;
      case ScriptActionType::Release:
        mappedInputManager.simulatorInjectRelease(action.button);
        break;
      case ScriptActionType::HomeTap:
        lastInjectedHomeAt = millis();
        simulatorHomeKeyInput.injectTap();
        break;
      case ScriptActionType::HomeLongPress:
        lastInjectedHomeAt = millis();
        simulatorHomeKeyInput.injectLongPress();
        break;
      case ScriptActionType::WaitForChapterSelection:
        if (!activityManager.isCurrentActivityNamed("EpubReaderChapterSelection")) {
          if (millis() - lastInjectedHomeAt > 1000) fail("Home shortcut did not open chapter selection");
          --scriptIndex;
        }
        break;
      case ScriptActionType::ConfigureChapterShortcuts:
        savedChapterShortcuts[0] = SETTINGS.shortPwrBtn;
        savedChapterShortcuts[1] = SETTINGS.longPwrBtn;
        savedChapterShortcuts[2] = SETTINGS.longPressMenuAction;
        savedChapterShortcuts[3] = SETTINGS.longPressBackAction;
        savedChapterShortcuts[4] = SETTINGS.homeButtonTapAction;
        savedChapterShortcuts[5] = SETTINGS.homeButtonLongPressAction;
        savedChapterShortcuts[6] = SETTINGS.homeButtonDoubleTapAction;
        SETTINGS.shortPwrBtn = CrossPointSettings::SELECT_CHAPTER;
        SETTINGS.longPwrBtn = CrossPointSettings::SELECT_CHAPTER;
        SETTINGS.longPressMenuAction = CrossPointSettings::LONG_MENU_SELECT_CHAPTER;
        SETTINGS.longPressBackAction = CrossPointSettings::LONG_MENU_OFF;
        SETTINGS.homeButtonTapAction = CrossPointSettings::SELECT_CHAPTER;
        SETTINGS.homeButtonLongPressAction = CrossPointSettings::SELECT_CHAPTER;
        SETTINGS.homeButtonDoubleTapAction = CrossPointSettings::IGNORE;
        break;
      case ScriptActionType::ConfigureChapterHomeDoubleTap:
        SETTINGS.homeButtonTapAction = CrossPointSettings::IGNORE;
        SETTINGS.homeButtonDoubleTapAction = CrossPointSettings::SELECT_CHAPTER;
        break;
      case ScriptActionType::RestoreChapterShortcuts:
        SETTINGS.shortPwrBtn = savedChapterShortcuts[0];
        SETTINGS.longPwrBtn = savedChapterShortcuts[1];
        SETTINGS.longPressMenuAction = savedChapterShortcuts[2];
        SETTINGS.longPressBackAction = savedChapterShortcuts[3];
        SETTINGS.homeButtonTapAction = savedChapterShortcuts[4];
        SETTINGS.homeButtonLongPressAction = savedChapterShortcuts[5];
        SETTINGS.homeButtonDoubleTapAction = savedChapterShortcuts[6];
        break;
      case ScriptActionType::WaitForMenuLongPress:
        if (mappedInputManager.getHeldTime() < 650) --scriptIndex;
        break;
      case ScriptActionType::ConfigureHomeButtonPowerLock:
        SETTINGS.homeButtonInReaderEnabled = 1;
        SETTINGS.shortPwrBtn = CrossPointSettings::SHORT_PWRBTN::TOGGLE_HOME_BUTTON_IN_READER;
        SETTINGS.longPwrBtn = CrossPointSettings::SHORT_PWRBTN::TOGGLE_HOME_BUTTON_IN_READER;
        break;
      case ScriptActionType::WaitForPowerLongPress:
        if (mappedInputManager.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
          --scriptIndex;
        }
        break;
      case ScriptActionType::AssertHomeButtonDisabled:
        if (SETTINGS.homeButtonInReaderEnabled) fail("Long Power did not disable the Home button");
        break;
      case ScriptActionType::AssertHomeButtonEnabled:
        if (!SETTINGS.homeButtonInReaderEnabled) fail("Long Power did not enable the Home button");
        break;
      case ScriptActionType::AssertTouchscreenDisabled:
        if (!SETTINGS.disableReaderTouchscreen) fail("Expected reader touchscreen to be disabled");
        break;
      case ScriptActionType::AssertTouchscreenEnabled:
        if (SETTINGS.disableReaderTouchscreen) fail("Expected reader touchscreen to be enabled");
        break;
      case ScriptActionType::AssertTtfProfileNative:
#if CROSSINK_SCALABLE_FONTS
        if (TTF_RENDER_PROFILES.profileFor(SETTINGS.sdFontFamilyName).hinting != 1) {
          fail("Expected active TTF profile to use native hinting");
        }
#endif
        break;
      case ScriptActionType::OpenSmokeBook: {
        const char* bookPath = std::getenv("CROSSINK_SIMULATOR_SMOKE_BOOK");
        if (bookPath == nullptr || bookPath[0] == '\0') fail("Smoke test book path is missing");
        activityManager.goToReader(bookPath, true);
        break;
      }
      case ScriptActionType::DisableReaderTouch:
        SETTINGS.disableReaderTouchscreen = true;
        break;
      case ScriptActionType::EnableReaderTouch:
        SETTINGS.disableReaderTouchscreen = false;
        break;
      case ScriptActionType::TouchDown:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchDown(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchDrawerHandle:
#if CROSSINK_APP_CAP_TOUCH
        if (auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity())) {
          const auto handle = panel->simulatorHandleRect();
          mappedInputManager.simulatorInjectTouchDown(handle.x + handle.width / 2, handle.y + handle.height / 2);
        } else if (auto* drawer = dynamic_cast<EpubReaderDrawerActivity*>(activityManager.simulatorCurrentActivity())) {
          const auto handle = drawer->simulatorHandleRect();
          mappedInputManager.simulatorInjectTouchDown(handle.x + handle.width / 2, handle.y + handle.height / 2);
        } else {
          fail("No visible drawer handle for touch gesture");
        }
#endif
        break;
      case ScriptActionType::TouchFrontlightQuickAction:
#if CROSSINK_APP_CAP_TOUCH
        if (auto* panel = dynamic_cast<FrontlightPanelActivity*>(activityManager.simulatorCurrentActivity())) {
          const auto hit = panel->simulatorQuickActionRect(action.x);
          if (hit.width <= 0 || hit.height <= 0) fail("Frontlight quick action has no published target");
          mappedInputManager.simulatorInjectTouchDown(hit.x + hit.width / 2, hit.y + hit.height / 2);
          // Queue the matching release before the next existing render step.
          inputScript.insert(inputScript.begin() + scriptIndex,
                             touchRelease(hit.x + hit.width / 2, hit.y + hit.height / 2));
        } else {
          fail("No Frontlight Panel for quick action");
        }
#endif
        break;
      case ScriptActionType::TouchMove:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchMove(action.x, action.y);
#endif
        break;
      case ScriptActionType::TouchRelease:
#if CROSSINK_APP_CAP_TOUCH
        mappedInputManager.simulatorInjectTouchRelease(action.x, action.y);
#endif
        break;
      case ScriptActionType::AssertReaderMenu: {
        if (!activityManager.isCurrentActivityNamed("EpubReaderDrawer"))
          fail("Expected reader menu for navigation assertion");
        const auto* drawer = static_cast<EpubReaderDrawerActivity*>(activityManager.simulatorCurrentActivity());
        const auto& state = drawer->simulatorState();
        if ((state.pane == ReaderDrawerPane::FontFamily || state.pane == ReaderDrawerPane::EnumOptions) &&
            !drawer->simulatorFocusedRowVisible())
          fail("Reader picker current choice is not highlighted and visible");
        if (static_cast<int>(state.tab) != action.x || static_cast<int>(state.pane) != action.y ||
            state.selectedIndex != action.settleFrames)
          fail("Reader menu navigation mismatch: tab=%d pane=%d row=%d, expected %d/%d/%d", static_cast<int>(state.tab),
               static_cast<int>(state.pane), state.selectedIndex, action.x, action.y, action.settleFrames);
        break;
      }
      case ScriptActionType::AssertSettingsNavigation: {
        if (!activityManager.isCurrentActivityNamed("Settings")) fail("Expected settings for navigation assertion");
        const auto* settings = static_cast<SettingsActivity*>(activityManager.simulatorCurrentActivity());
        if (settings->simulatorCategoryIndex() != action.x || settings->simulatorSelectedIndex() != action.y)
          fail("Settings navigation mismatch: category=%d row=%d, expected %d/%d", settings->simulatorCategoryIndex(),
               settings->simulatorSelectedIndex(), action.x, action.y);
        break;
      }
      case ScriptActionType::AssertMenuNavigation: {
        RenderLock lock;
        if (SETTINGS.menuNavigation != action.x) fail("Menu navigation selection failed");
        SETTINGS.menuNavigation = 255;
        if (!SETTINGS.loadFromFile() || SETTINGS.menuNavigation != action.x)
          fail("Menu navigation did not survive reloading saved settings");
        break;
      }
      case ScriptActionType::WaitForSettingsCategory: {
        const auto* settings = dynamic_cast<SettingsActivity*>(activityManager.simulatorCurrentActivity());
        if (!settings) fail("Expected Settings during classic hold");
        if (settings->simulatorCategoryIndex() != action.x) --scriptIndex;
        break;
      }
      case ScriptActionType::CaptureMenuNavigation: {
        RenderLock lock;
        captureStatusBarScreen(action.label);
        break;
      }
      case ScriptActionType::WaitForNavigationHold:
        if (mappedInputManager.getHeldTime() < 650) --scriptIndex;
        break;
      case ScriptActionType::AssertAboutTopIndex: {
        const auto* about = dynamic_cast<AboutActivity*>(activityManager.simulatorCurrentActivity());
        if (!about || about->simulatorTopIndex() != action.x)
          fail("About button moved to row %d, expected %d", about ? about->simulatorTopIndex() : -1, action.x);
        break;
      }
      case ScriptActionType::AssertActivity:
        if (!activityManager.isCurrentActivityNamed(action.label)) fail("Expected current activity: %s", action.label);
        break;
      case ScriptActionType::Render:
        queueStep(action.label, SmokeStep::ReaderInput, action.settleFrames);
        break;
    }
  }
};

SimulatorSmokeTest smokeTest;

}  // namespace

void runSimulatorSmokeTestTick() { smokeTest.tick(); }

#endif

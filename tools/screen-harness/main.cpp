#include <EpdFont.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <SPI.h>
#include <builtinFonts/all.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/RomanClockFontRenderer.h"
#include "activities/settings/FactoryResetActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "util/RecentBooksStore.h"

namespace {

std::filesystem::path gSettingsJsonPath;

void installFonts(GfxRenderer& renderer) {
  static EpdFont smallFont(&ubuntu_10_regular);
  static EpdFontFamily smallFontFamily(&smallFont);

  static EpdFont ui10RegularFont(&ubuntu_10_regular);
  static EpdFont ui10BoldFont(&ubuntu_10_bold);
  static EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

  static EpdFont ui12RegularFont(&ubuntu_12_regular);
  static EpdFont ui12BoldFont(&ubuntu_12_bold);
  static EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

  static EpdFont notoserif14RegularFont(&notoserif_14_regular);
  static EpdFont notoserif14BoldFont(&notoserif_14_bold);
  static EpdFont notoserif14ItalicFont(&notoserif_14_italic);
  static EpdFont notoserif14BoldItalicFont(&notoserif_14_bolditalic);
  static EpdFontFamily notoserif14FontFamily(&notoserif14RegularFont, &notoserif14BoldFont, &notoserif14ItalicFont,
                                             &notoserif14BoldItalicFont);

  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14FontFamily);
}

void saveSnapshot(HalDisplay& display, const std::filesystem::path& outDir, const std::string& name) {
  const auto outputPath = outDir / (name + ".pbm");
  const auto outputPathStr = outputPath.string();
  display.saveFrameBufferAsPBM(outputPathStr.c_str());
  std::cout << "wrote " << outputPathStr << '\n';
}

std::string readTextFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) return "";
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool readJsonNumber(const std::string& json, const char* key, uint8_t& out) {
  const std::regex pattern(std::string("\"") + key + R"("\s*:\s*([0-9]+))");
  std::smatch match;
  if (!std::regex_search(json, match, pattern)) return false;
  out = static_cast<uint8_t>(std::clamp(std::stoi(match[1].str()), 0, 255));
  return true;
}

bool readJsonString(const std::string& json, const char* key, char* out, size_t outSize) {
  const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(json, match, pattern) || outSize == 0) return false;
  std::strncpy(out, match[1].str().c_str(), outSize - 1);
  out[outSize - 1] = '\0';
  return true;
}

void seedSettingsFromConfiguratorDefaults() {
  SETTINGS.fontFamily = 0;
  SETTINGS.fontSize = 1;
  SETTINGS.lineSpacing = 1;
  SETTINGS.screenMargin = 5;
  SETTINGS.paragraphAlignment = 0;
  SETTINGS.embeddedStyle = 1;
  SETTINGS.hyphenationEnabled = 0;
  SETTINGS.orientation = 0;
  SETTINGS.extraParagraphSpacing = 1;
  SETTINGS.textAntiAliasing = 1;
  SETTINGS.imageRendering = 0;
  SETTINGS.frontButtonLayout = 0;
  SETTINGS.frontButtonBack = 0;
  SETTINGS.frontButtonConfirm = 1;
  SETTINGS.frontButtonLeft = 2;
  SETTINGS.frontButtonRight = 3;
  SETTINGS.sleepScreen = 0;
  SETTINGS.sleepScreenSource = 0;
  SETTINGS.sleepScreenCoverMode = 0;
  SETTINGS.sleepScreenCoverFilter = 0;
  SETTINGS.sleepCycleMode = 0;
  SETTINGS.darkMode = 0;
  SETTINGS.hideBatteryPercentage = 0;
  SETTINGS.uiTheme = 1;
  SETTINGS.refreshFrequency = 3;
  SETTINGS.fadingFix = 0;
  SETTINGS.globalStatusBar = 0;
  SETTINGS.globalStatusBarPosition = 0;
  SETTINGS.sleepTimeout = 2;
  SETTINGS.sideButtonLayout = 0;
  SETTINGS.shortPwrBtn = 0;
  SETTINGS.longPressChapterSkip = 1;
  SETTINGS.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  SETTINGS.usbMscPromptOnConnect = 0;
  SETTINGS.backgroundServerOnCharge = 1;
  SETTINGS.wifiAutoConnect = 0;
  SETTINGS.deviceName[0] = '\0';
}

void applySettingsJson(const std::filesystem::path& settingsJsonPath) {
  seedSettingsFromConfiguratorDefaults();
  if (settingsJsonPath.empty()) return;

  const std::string json = readTextFile(settingsJsonPath);
  if (json.empty()) {
    std::cerr << "settings json not found or empty: " << settingsJsonPath << '\n';
    return;
  }

  readJsonNumber(json, "fontFamily", SETTINGS.fontFamily);
  readJsonNumber(json, "fontSize", SETTINGS.fontSize);
  readJsonNumber(json, "lineSpacing", SETTINGS.lineSpacing);
  readJsonNumber(json, "screenMargin", SETTINGS.screenMargin);
  readJsonNumber(json, "paragraphAlignment", SETTINGS.paragraphAlignment);
  readJsonNumber(json, "embeddedStyle", SETTINGS.embeddedStyle);
  readJsonNumber(json, "hyphenationEnabled", SETTINGS.hyphenationEnabled);
  readJsonNumber(json, "orientation", SETTINGS.orientation);
  readJsonNumber(json, "extraParagraphSpacing", SETTINGS.extraParagraphSpacing);
  readJsonNumber(json, "textAntiAliasing", SETTINGS.textAntiAliasing);
  readJsonNumber(json, "imageRendering", SETTINGS.imageRendering);
  readJsonNumber(json, "frontButtonLayout", SETTINGS.frontButtonLayout);
  readJsonNumber(json, "frontButtonBack", SETTINGS.frontButtonBack);
  readJsonNumber(json, "frontButtonConfirm", SETTINGS.frontButtonConfirm);
  readJsonNumber(json, "frontButtonLeft", SETTINGS.frontButtonLeft);
  readJsonNumber(json, "frontButtonRight", SETTINGS.frontButtonRight);
  readJsonNumber(json, "sleepScreen", SETTINGS.sleepScreen);
  readJsonNumber(json, "sleepScreenSource", SETTINGS.sleepScreenSource);
  readJsonNumber(json, "sleepScreenCoverMode", SETTINGS.sleepScreenCoverMode);
  readJsonNumber(json, "sleepScreenCoverFilter", SETTINGS.sleepScreenCoverFilter);
  readJsonNumber(json, "sleepCycleMode", SETTINGS.sleepCycleMode);
  readJsonNumber(json, "darkMode", SETTINGS.darkMode);
  readJsonNumber(json, "hideBatteryPercentage", SETTINGS.hideBatteryPercentage);
  readJsonNumber(json, "uiTheme", SETTINGS.uiTheme);
  readJsonNumber(json, "refreshFrequency", SETTINGS.refreshFrequency);
  readJsonNumber(json, "fadingFix", SETTINGS.fadingFix);
  readJsonNumber(json, "globalStatusBar", SETTINGS.globalStatusBar);
  readJsonNumber(json, "globalStatusBarPosition", SETTINGS.globalStatusBarPosition);
  readJsonNumber(json, "sleepTimeout", SETTINGS.sleepTimeout);
  readJsonNumber(json, "sideButtonLayout", SETTINGS.sideButtonLayout);
  readJsonNumber(json, "shortPwrBtn", SETTINGS.shortPwrBtn);
  readJsonNumber(json, "longPressChapterSkip", SETTINGS.longPressChapterSkip);
  readJsonNumber(json, "longPressButtonBehavior", SETTINGS.longPressButtonBehavior);
  readJsonNumber(json, "usbMscPromptOnConnect", SETTINGS.usbMscPromptOnConnect);
  readJsonNumber(json, "backgroundServerOnCharge", SETTINGS.backgroundServerOnCharge);
  readJsonNumber(json, "wifiAutoConnect", SETTINGS.wifiAutoConnect);
  readJsonString(json, "deviceName", SETTINGS.deviceName, sizeof(SETTINGS.deviceName));

  if (SETTINGS.longPressChapterSkip && SETTINGS.longPressButtonBehavior == CrossPointSettings::OFF) {
    SETTINGS.longPressButtonBehavior = CrossPointSettings::CHAPTER_SKIP;
  }
}

void drawHeader(GfxRenderer& renderer, const char* title) {
  const int right = renderer.getScreenWidth() - 18;
  renderer.drawText(UI_12_FONT_ID, 18, 14, title, true, EpdFontFamily::BOLD);
  renderer.drawLine(18, 40, right, 40);
}

void drawLockIcon(GfxRenderer& renderer, int cx, int cy) {
  renderer.fillRect(cx - 13, cy - 13, 26, 22, false);
  renderer.drawLine(cx - 5, cy - 1, cx - 5, cy - 10);
  renderer.drawLine(cx + 5, cy - 1, cx + 5, cy - 10);
  renderer.drawLine(cx - 5, cy - 10, cx + 5, cy - 10);
  renderer.fillRect(cx - 9, cy, 18, 12, true);
  renderer.fillRect(cx - 8, cy + 1, 16, 10, false);
  renderer.fillRect(cx - 1, cy + 3, 3, 5, true);
}

void drawBoot(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  BootActivity boot(renderer, mappedInput);
  boot.onEnter();
}

std::vector<RecentBook> sampleBooks(int count) {
  static constexpr const char* kTitles[] = {
      "Left Hand of Darkness", "Dune", "Foundation", "Neuromancer", "Name of the Wind", "Ancillary Justice",
  };
  static constexpr const char* kAuthors[] = {
      "Le Guin", "Frank Herbert", "Isaac Asimov", "W. Gibson", "Rothfuss", "Ann Leckie",
  };

  std::vector<RecentBook> books;
  books.reserve(count);
  for (int i = 0; i < count; ++i) {
    books.push_back({
        std::string("/books/mock-") + std::to_string(i) + ".epub",
        kTitles[i % static_cast<int>(std::size(kTitles))],
        kAuthors[i % static_cast<int>(std::size(kAuthors))],
        "",
    });
  }
  return books;
}

struct HomePreviewScenario {
  std::string name;
  CrossPointSettings::UI_THEME theme;
  std::vector<RecentBook> books;
  std::vector<std::string> menuLabels;
  std::vector<UIIcon> menuIcons;
  int selectedBookIndex = 0;
  int selectedMenuIndex = -1;
  bool inButtonGrid = true;
  const char* backLabel = "";
  const char* upLabel = "Prev";
  const char* downLabel = "Next";
};

void drawHomeThemePreview(GfxRenderer& renderer, const HomePreviewScenario& scenario) {
  SETTINGS.uiTheme = static_cast<uint8_t>(scenario.theme);
  SETTINGS.globalStatusBar = CrossPointSettings::GLOBAL_STATUS_BAR_OFF;
  std::strncpy(SETTINGS.deviceName, "crosspoint", sizeof(SETTINGS.deviceName) - 1);
  SETTINGS.deviceName[sizeof(SETTINGS.deviceName) - 1] = '\0';

  auto& uiTheme = UITheme::getInstance();
  uiTheme.reload();
  const auto& metrics = uiTheme.getMetrics();
  const auto& theme = uiTheme.getTheme();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(scenario.books.size());
  const bool forkDriftLayout =
      scenario.theme == CrossPointSettings::FORK_DRIFT || scenario.theme == CrossPointSettings::POKEMON_PARTY;
  const int singleRowHeight = metrics.homeCoverTileHeight / 2;
  const int rawCoverTileHeight = forkDriftLayout ? ((bookCount > 3 ? 2 : 1) * singleRowHeight)
                                                 : metrics.homeCoverTileHeight;
  const int menuMinHeight = metrics.verticalSpacing * 2 + metrics.buttonHintsHeight + metrics.menuRowHeight;
  const int coverTileHeight = forkDriftLayout ? std::min(rawCoverTileHeight, pageHeight - menuMinHeight)
                                              : rawCoverTileHeight;
  const int menuRectY = coverTileHeight + metrics.verticalSpacing;
  const int menuRectHeight = pageHeight - (coverTileHeight + metrics.verticalSpacing * 2 + metrics.buttonHintsHeight);
  const int coverSelector = forkDriftLayout && scenario.inButtonGrid ? -1 : scenario.selectedBookIndex;
  const int menuSelector = forkDriftLayout && !scenario.inButtonGrid ? -1 : scenario.selectedMenuIndex;

  bool coverRendered = false;
  bool coverBufferStored = false;
  bool bufferRestored = false;

  renderer.clearScreen();
  theme.drawRecentBookCover(
      renderer, Rect{0, 0, pageWidth, coverTileHeight}, scenario.books, coverSelector,
      coverRendered, coverBufferStored, bufferRestored, []() { return false; }, 57.0f);
  theme.drawButtonMenu(
      renderer, Rect{0, menuRectY, pageWidth, menuRectHeight}, static_cast<int>(scenario.menuLabels.size()),
      menuSelector, [&scenario](int index) { return scenario.menuLabels[index]; },
      [&scenario](int index) { return scenario.menuIcons[index]; });
  theme.drawButtonHints(renderer, scenario.backLabel, "Select", scenario.upLabel, scenario.downLabel);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawSettingsMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Settings");

  const auto drawGroupLabel = [&](int y, const char* label) {
    renderer.drawText(SMALL_FONT_ID, 24, y, label, true, EpdFontFamily::BOLD);
  };
  const auto drawRow = [&](int y, const char* label, const char* value) {
    renderer.drawRoundedRect(24, y, 432, 42, 1, 6, true);
    renderer.drawText(UI_10_FONT_ID, 34, y + 14, label, true);
    renderer.drawText(UI_10_FONT_ID, 300, y + 14, value, true, EpdFontFamily::BOLD);
  };

  drawGroupLabel(60, "Reading");
  drawRow(76, "Font Family", "Noto Serif");
  drawRow(122, "Font Size", "14 pt");
  drawRow(168, "Line Spacing", "Normal");
  drawRow(214, "Screen Margin", "Medium");

  drawGroupLabel(266, "Display");
  drawRow(282, "UI Theme", "ForkDrift");
  drawRow(328, "E-Ink Refresh", "5 pages");
  drawRow(374, "Orientation", "Portrait");

  drawGroupLabel(426, "Network");
  drawRow(442, "WiFi Network", "HomeNetwork");
  drawRow(488, "Auto-Connect", "On");

  drawGroupLabel(540, "Sleep");
  drawRow(556, "Sleep Screen", "Custom");
  drawRow(602, "Source Folder", "/sleep");

  drawGroupLabel(654, "About");
  drawRow(670, "Device Name", "crosspoint");
  drawRow(716, "Firmware", "v0.9.2");

  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Edit", "Prev", "Next");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawSettings(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  applySettingsJson(gSettingsJsonPath);
  UITheme::getInstance().reload();
  SettingsActivity activity(renderer, mappedInput);
  activity.onEnter();
  activity.render(RenderLock(activity));
}

void drawFactoryResetMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Factory Reset");

  renderer.drawRoundedRect(20, 56, 440, 580, 2, 8, true);

  int y = 76;

  renderer.drawCenteredText(UI_12_FONT_ID, y, "Permanent Action", true, EpdFontFamily::BOLD);
  y += 34;

  renderer.drawLine(34, y, 446, y);
  y += 16;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "The following data will be erased:", true);
  y += 26;

  static constexpr const char* erased[] = {
      "• Settings and preferences", "• WiFi and network credentials", "• Reading progress and bookmarks",
      "• Daily notes and todo entries", "• Feature store configuration", "• Anki cards and study data",
      "• Cover image cache", "• EPUB layout cache",
  };
  for (const char* item : erased) {
    renderer.drawText(UI_10_FONT_ID, 38, y, item, true);
    y += 22;
  }

  y += 10;
  renderer.drawLine(34, y, 446, y);
  y += 16;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "Books and files on the SD card", true, EpdFontFamily::BOLD);
  y += 24;
  renderer.drawCenteredText(UI_10_FONT_ID, y, "will NOT be deleted.", true, EpdFontFamily::BOLD);
  y += 34;

  renderer.drawLine(34, y, 446, y);
  y += 16;

  renderer.drawCenteredText(UI_10_FONT_ID, y, "This action cannot be undone.", true);
  y += 34;

  renderer.drawRoundedRect(34, y, 190, 48, 1, 6, true);
  renderer.drawText(UI_10_FONT_ID, 90, y + 17, "Cancel", true);
  renderer.drawRoundedRect(236, y, 190, 48, 2, 6, true);
  renderer.drawText(UI_10_FONT_ID, 282, y + 17, "Reset Device", true, EpdFontFamily::BOLD);

  renderer.drawButtonHints(UI_10_FONT_ID, "Cancel", "Reset", "", "");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawFactoryReset(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  applySettingsJson(gSettingsJsonPath);
  UITheme::getInstance().reload();
  FactoryResetActivity activity(renderer, mappedInput, [] {});
  activity.onEnter();
  activity.render(RenderLock(activity));
}

void drawReaderMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  drawHeader(renderer, "Reader");

  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 62, "We walked through the quiet city as", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 88, "the daylight thinned into a pale", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 114, "silver dusk over the harbor.", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 140, "", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 166, "No one spoke. The only sound was", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 192, "the wind pressing at the shutters", true);
  renderer.drawText(NOTOSERIF_14_FONT_ID, 28, 218, "and the pages turning in my hand.", true);

  renderer.drawRect(28, 760, 424, 10, 1, true);
  renderer.fillRect(30, 762, 242, 6, true);
  renderer.drawText(SMALL_FONT_ID, 30, 736, "Chapter 8", true);
  renderer.drawText(SMALL_FONT_ID, 394, 736, "57%", true);

  renderer.drawButtonHints(UI_10_FONT_ID, "Menu", "Select", "Prev", "Next");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void drawSleepBrandScreen(GfxRenderer& renderer, bool lightScreen) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "ForkDrift", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "SLEEPING");

  if (!lightScreen) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void drawSleepCustomMock(GfxRenderer& renderer) {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.fillRect(18, 18, pageWidth - 36, pageHeight - 36, true);
  renderer.drawRoundedRect(34, 34, pageWidth - 68, pageHeight - 68, 2, 14, false);
  renderer.drawCenteredText(UI_12_FONT_ID, 82, "CUSTOM SLEEP IMAGE", false, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, 108, "preview from /sleep/night-sky.png", false);

  renderer.drawRect(88, 160, 304, 188, 2, false);
  renderer.drawLine(118, 324, 208, 212);
  renderer.drawLine(208, 212, 262, 276);
  renderer.drawLine(262, 276, 360, 196);
  renderer.drawLine(360, 196, 392, 230);
  renderer.fillRect(124, 224, 42, 42, false);
  renderer.fillRect(300, 250, 26, 26, false);
  renderer.fillRect(344, 110, 8, 8, false);
  renderer.fillRect(120, 116, 6, 6, false);
  renderer.fillRect(166, 92, 4, 4, false);
  renderer.fillRect(276, 128, 5, 5, false);
  renderer.fillRect(214, 74, 7, 7, false);

  renderer.drawRoundedRect(88, 412, 304, 210, 2, 12, false);
  renderer.drawCenteredText(UI_12_FONT_ID, 446, "CURRENT BOOK COVER", false, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 484, "\"Left Hand of Darkness\"", false);
  renderer.drawCenteredText(SMALL_FONT_ID, 512, "Fit: Crop", false);
  renderer.drawCenteredText(SMALL_FONT_ID, 534, "Filter: None", false);
  renderer.drawLine(132, 564, 348, 564, false);
  renderer.drawCenteredText(SMALL_FONT_ID, 590, "Custom sleep art fills the panel", false);

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void drawSleepTransparentMock(GfxRenderer& renderer) {
  drawReaderMock(renderer);
  drawLockIcon(renderer, renderer.getScreenWidth() / 2, renderer.getScreenHeight() - 14);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void drawSleepRomanClockMock(GfxRenderer& renderer, const std::string& labelText) {
  const RomanClockFontRenderer::LabelParts label = RomanClockFontRenderer::splitLabel(labelText);
  if (label.hour.empty()) {
    drawSleepBrandScreen(renderer, false);
    return;
  }

  if (RomanClockFontRenderer::getFontData(renderer) == nullptr) {
    drawSleepBrandScreen(renderer, false);
    return;
  }

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();

  static constexpr int kFrameMargin = 28;
  static constexpr int kFrameRadius = 12;
  renderer.drawRoundedRect(kFrameMargin, kFrameMargin, pageWidth - kFrameMargin * 2, pageHeight - kFrameMargin * 2, 1,
                           kFrameRadius, true);

  static constexpr int kInnerPad = 32;
  const int contentX = kFrameMargin + kInnerPad;
  const int contentWidth = pageWidth - contentX * 2;
  const int contentY = kFrameMargin + kInnerPad;
  const int contentHeight = pageHeight - contentY * 2;

  const bool hasMinute = !label.minute.empty();
  if (!hasMinute) {
    const int hourScale = RomanClockFontRenderer::fitTextScale(renderer, label.hour, contentWidth, contentHeight * 7 / 12);
    const int hourHeight = RomanClockFontRenderer::baseTextHeight(renderer) * hourScale;
    const int hourWidth = RomanClockFontRenderer::scaledTextWidth(renderer, label.hour, hourScale);
    RomanClockFontRenderer::drawScaledText(renderer, label.hour, contentX + (contentWidth - hourWidth) / 2,
                                           contentY + (contentHeight - hourHeight) / 2, hourScale);
  } else {
    const int hourZoneTopY = contentY + contentHeight * 5 / 100;
    const int hourZoneHeight = contentHeight * 53 / 100;
    const int ruleY = contentY + contentHeight * 64 / 100;
    const int minuteZoneY = contentY + contentHeight * 68 / 100;
    const int minuteZoneHeight = contentHeight * 24 / 100;

    const int hourScale = RomanClockFontRenderer::fitTextScale(renderer, label.hour, contentWidth, hourZoneHeight);
    const int hourHeight = RomanClockFontRenderer::baseTextHeight(renderer) * hourScale;
    const int hourWidth = RomanClockFontRenderer::scaledTextWidth(renderer, label.hour, hourScale);
    RomanClockFontRenderer::drawScaledText(renderer, label.hour, contentX + (contentWidth - hourWidth) / 2,
                                           hourZoneTopY + (hourZoneHeight - hourHeight) / 2, hourScale);

    const int ruleWidth = contentWidth * 3 / 10;
    renderer.fillRect(contentX + (contentWidth - ruleWidth) / 2, ruleY, ruleWidth, 1, true);

    const int minuteMaxHeight = std::min(minuteZoneHeight, hourHeight / 2);
    const int minuteScale = RomanClockFontRenderer::fitTextScale(renderer, label.minute, contentWidth, minuteMaxHeight);
    const int minuteHeight = RomanClockFontRenderer::baseTextHeight(renderer) * minuteScale;
    const int minuteWidth = RomanClockFontRenderer::scaledTextWidth(renderer, label.minute, minuteScale);
    RomanClockFontRenderer::drawScaledText(renderer, label.minute, contentX + (contentWidth - minuteWidth) / 2,
                                           minuteZoneY + (minuteZoneHeight - minuteHeight) / 2, minuteScale);
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void drawFeatureStoreMock(GfxRenderer& renderer) {
  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, "Update", true, EpdFontFamily::BOLD);

  renderer.drawText(UI_10_FONT_ID, 15, 55, "Feature Store", true, EpdFontFamily::BOLD);
  const char* counter = "3 / 5";
  const int counterW = renderer.getTextWidth(UI_10_FONT_ID, counter);
  renderer.drawText(UI_10_FONT_ID, renderer.getScreenWidth() - counterW - 15, 55, counter);

  const int cardX = 15;
  const int cardY = 78;
  const int cardW = renderer.getScreenWidth() - 30;
  const int cardH = 510;
  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 8, true);

  int y = cardY + 24;
  const int textX = cardX + 14;

  renderer.drawText(UI_12_FONT_ID, textX, y, "Latest Standard (on push)", true, EpdFontFamily::BOLD);
  y += 32;
  renderer.drawText(UI_10_FONT_ID, textX, y, "* Installed *");
  y += 22;
  renderer.drawText(UI_10_FONT_ID, textX, y, "dev");
  y += 26;

  renderer.drawLine(cardX + 10, y, cardX + cardW - 10, y);
  y += 15;

  const char* features[] = {"• EPUB", "• FONTS", "• OTA", "• DARK MODE", "• BLE SETUP", "• WEB SETUP", "• USB STORAGE"};
  for (const auto* feature : features) {
    renderer.drawText(UI_10_FONT_ID, textX + 6, y, feature);
    y += 20;
  }

  renderer.drawButtonHints(UI_10_FONT_ID, "Back", "Select", "Prev", "Next");
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace

int main(int argc, char* argv[]) {
  const std::filesystem::path outputDir = (argc > 1) ? argv[1] : "build/screen-previews";
  const char* settingsJsonEnv = std::getenv("SCREEN_PREVIEW_SETTINGS_JSON");
  const std::filesystem::path settingsJsonPath =
      (argc > 2) ? std::filesystem::path(argv[2])
                 : (settingsJsonEnv != nullptr ? std::filesystem::path(settingsJsonEnv) : std::filesystem::path{});
  gSettingsJsonPath = settingsJsonPath;
  std::filesystem::create_directories(outputDir);
  applySettingsJson(settingsJsonPath);

  HalDisplay display;
  display.begin();

  GfxRenderer renderer(display);
  renderer.begin();
  FontDecompressor fontDecompressor;
  if (!fontDecompressor.init()) {
    std::cerr << "failed to initialize FontDecompressor\n";
    return 1;
  }
  installFonts(renderer);
  FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);

  HalGPIO gpio;
  MappedInputManager mappedInput(gpio);

  const std::vector<HomePreviewScenario> homeScenarios = {
      {"02_home_classic", CrossPointSettings::CLASSIC, sampleBooks(1), {"Open Book", "My Library", "File Transfer", "Settings"},
       {Book, Folder, Transfer, Settings}, 0, 0, true, "", "Prev", "Next"},
      {"03_home_lyra", CrossPointSettings::LYRA, sampleBooks(1), {"Open Book", "My Library", "File Transfer", "Settings"},
       {Book, Folder, Transfer, Settings}, 0, 0, true, "", "Prev", "Next"},
      {"04_home_visual_covers", CrossPointSettings::LYRA_EXTENDED, sampleBooks(3),
       {"Open Book", "My Library", "File Transfer", "Settings"}, {Book, Folder, Transfer, Settings}, 1, 0, true, "",
       "Prev", "Next"},
      {"05_home_forkdrift", CrossPointSettings::FORK_DRIFT, sampleBooks(6), {"Books", "Agenda", "File Transfer", "Settings"},
       {Folder, Text, Transfer, Settings}, 0, 0, true, "", "Up", "Down"},
      {"06_home_pokemon_party", CrossPointSettings::POKEMON_PARTY, sampleBooks(6), {"Settings"}, {Settings}, -1, 0,
       true, "Party", "Up", ""},
      {"07_home_minimal", CrossPointSettings::MINIMAL, sampleBooks(1), {"Open Book", "My Library", "File Transfer", "Settings"},
       {Book, Folder, Transfer, Settings}, 0, 0, true, "", "Prev", "Next"},
      {"08_home_lyra_carousel", CrossPointSettings::LYRA_CAROUSEL, sampleBooks(6),
       {"Open Book", "My Library", "File Transfer", "Settings"}, {Book, Folder, Transfer, Settings}, 2, 0, true, "",
       "Prev", "Next"},
  };

  const std::vector<std::pair<std::string, std::function<void()>>> scenarios = {
      {"01_boot", [&] { drawBoot(renderer, mappedInput); }},
      {"02_home_classic", [&] { drawHomeThemePreview(renderer, homeScenarios[0]); }},
      {"03_home_lyra", [&] { drawHomeThemePreview(renderer, homeScenarios[1]); }},
      {"04_home_visual_covers", [&] { drawHomeThemePreview(renderer, homeScenarios[2]); }},
      {"05_home_forkdrift", [&] { drawHomeThemePreview(renderer, homeScenarios[3]); }},
      {"06_home_pokemon_party", [&] { drawHomeThemePreview(renderer, homeScenarios[4]); }},
      {"07_home_minimal", [&] { drawHomeThemePreview(renderer, homeScenarios[5]); }},
      {"08_home_lyra_carousel", [&] { drawHomeThemePreview(renderer, homeScenarios[6]); }},
      {"09_settings", [&] { drawSettings(renderer, mappedInput); }},
      {"10_factory_reset", [&] { drawFactoryReset(renderer, mappedInput); }},
      {"11_reader_mock", [&] { drawReaderMock(renderer); }},
      {"12_feature_store_mock", [&] { drawFeatureStoreMock(renderer); }},
      {"13_sleep_dark", [&] { drawSleepBrandScreen(renderer, false); }},
      {"14_sleep_light", [&] { drawSleepBrandScreen(renderer, true); }},
      {"15_sleep_custom", [&] { drawSleepCustomMock(renderer); }},
      {"16_sleep_transparent", [&] { drawSleepTransparentMock(renderer); }},
      {"17_sleep_roman_clock", [&] { drawSleepRomanClockMock(renderer, "XII:III"); }},
  };

  for (const auto& [name, render] : scenarios) {
    render();
    saveSnapshot(display, outputDir, name);
  }

  return 0;
}

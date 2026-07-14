#include "network/server/SettingsSnapshotApi.h"

#include <ArduinoJson.h>

#include "CrossPointSettings.h"

namespace network {

String buildSettingsSnapshotJson(const CrossPointSettings& s) {
  JsonDocument doc;
  doc["sleepScreen"] = s.sleepScreen;
  doc["sleepScreenSource"] = s.sleepScreenSource;
  doc["sleepPinnedPath"] = s.sleepPinnedPath;
  doc["sleepScreenCoverMode"] = s.sleepScreenCoverMode;
  doc["sleepScreenCoverFilter"] = s.sleepScreenCoverFilter;
  doc["statusBarChapterPageCount"] = s.statusBarChapterPageCount;
  doc["statusBarBookProgressPercentage"] = s.statusBarBookProgressPercentage;
  doc["statusBarProgressBar"] = s.statusBarProgressBar;
  doc["statusBarProgressBarThickness"] = s.statusBarProgressBarThickness;
  doc["statusBarTitle"] = s.statusBarTitle;
  doc["statusBarBattery"] = s.statusBarBattery;
  doc["extraParagraphSpacing"] = s.extraParagraphSpacing;
  doc["textAntiAliasing"] = s.textAntiAliasing;
  doc["shortPwrBtn"] = s.shortPwrBtn;
#if ENABLE_DOUBLE_TAP_ACTION
  doc["doubleTapPwrBtn"] = s.doubleTapPwrBtn;
#endif
  doc["orientation"] = s.orientation;
  doc["frontButtonLayout"] = s.frontButtonLayout;
  doc["sideButtonLayout"] = s.sideButtonLayout;
  doc["frontButtonBack"] = s.frontButtonBack;
  doc["frontButtonConfirm"] = s.frontButtonConfirm;
  doc["frontButtonLeft"] = s.frontButtonLeft;
  doc["frontButtonRight"] = s.frontButtonRight;
  doc["fontFamily"] = s.fontFamily;
  doc["fontSize"] = s.fontSize;
  doc["lineSpacing"] = s.lineSpacing;
  doc["paragraphAlignment"] = s.paragraphAlignment;
  doc["sleepTimeoutMinutes"] = s.sleepTimeoutMinutes;
  doc["refreshFrequency"] = s.refreshFrequency;
  doc["screenMargin"] = s.screenMargin;
  doc["hideBatteryPercentage"] = s.hideBatteryPercentage;
  doc["longPressButtonBehavior"] = s.longPressButtonBehavior;
  doc["hyphenationEnabled"] = s.hyphenationEnabled;
  doc["backgroundServerOnCharge"] = s.backgroundServerOnCharge;
  doc["timeMode"] = s.timeMode;
  doc["timeZoneOffset"] = s.timeZoneOffset;
  doc["releaseChannel"] = s.releaseChannel;
  doc["uiTheme"] = s.uiTheme;
  doc["fadingFix"] = s.fadingFix;
  doc["darkMode"] = s.darkMode;
  doc["embeddedStyle"] = s.embeddedStyle;
  doc["userFontPath"] = s.userFontPath;
  doc["selectedOtaBundle"] = s.selectedOtaBundle;
  doc["installedOtaBundle"] = s.installedOtaBundle;
  doc["deviceName"] = s.deviceName;

  String json;
  serializeJson(doc, json);
  return json;
}

}  // namespace network

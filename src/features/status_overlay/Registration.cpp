#include "features/status_overlay/Registration.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <WiFi.h>

#include <algorithm>
#include <string>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "core/registries/LifecycleRegistry.h"
#include "features/status_overlay/Layout.h"
#include "features/status_overlay/ReaderContext.h"
#include "fontIds.h"
#include "network/BackgroundWebServer.h"
#if ENABLE_WIFI_CLOCK
#include "util/DateUtils.h"
#endif

namespace features::status_overlay {

#if ENABLE_GLOBAL_STATUS_BAR

namespace {

void drawWifiIcon(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int cx = 7;
  constexpr int cy = 13;
  renderer.drawArc(7, x + cx, y + cy, -1, -1, 2, true);
  renderer.drawArc(7, x + cx, y + cy, 1, -1, 2, true);
  renderer.drawArc(4, x + cx, y + cy, -1, -1, 2, true);
  renderer.drawArc(4, x + cx, y + cy, 1, -1, 2, true);
  renderer.fillRect(x + cx - 1, y + cy - 1, 3, 3, true);
}

void drawSyncIcon(const GfxRenderer& renderer, const int x, const int y) {
  renderer.drawLine(x + 4, y + 5, x + 11, y + 5, 2, true);
  renderer.drawLine(x + 11, y + 5, x + 8, y + 2, 2, true);
  renderer.drawLine(x + 11, y + 5, x + 8, y + 8, 2, true);

  renderer.drawLine(x + 11, y + 11, x + 4, y + 11, 2, true);
  renderer.drawLine(x + 4, y + 11, x + 7, y + 8, 2, true);
  renderer.drawLine(x + 4, y + 11, x + 7, y + 14, 2, true);
}

void drawStatusOverlay(const GfxRenderer& renderer) {
  if (!isEnabled()) {
    return;
  }

  const wifi_mode_t mode = WiFi.getMode();
  const bool isWifiConnected =
      (mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  const bool shouldShowIp = isWifiConnected && activityManager.showsStatusBarIp();
  const bool isFileServerRunning = BackgroundWebServer::getInstance().isRunning();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int kTextGap = 12;
  const int barH = barHeight();
  const int padTopPx = padTop();
  const int padHPx = padH();
  const int barY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? screenH - barH : 0;
  const int sepY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? barY : barY + barH - 1;
  const int textY = barY + padTopPx;

  renderer.fillRect(0, barY, screenW, barH, false);
  renderer.drawLine(0, sepY, screenW - 1, sepY, true);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryGroupWidth =
      metrics.batteryWidth +
      (showBatteryPercentage ? renderer.getTextWidth(SMALL_FONT_ID, "100%") + BaseTheme::batteryPercentSpacing : 0);
  int textRightLimit = screenW - padHPx - batteryGroupWidth;
  const int iconY = barY + padTopPx;
  if (isWifiConnected) {
    textRightLimit -= kStatusIconSize;
  }
  if (isFileServerRunning) {
    textRightLimit -= isWifiConnected ? kStatusIconGap + kStatusIconSize : kStatusIconSize;
  }
  const int leftTextX = padHPx;

  // Reading context: the reader publishes here instead of drawing a second bar.
  // When a reader page is shown the band prioritises book info (progress + title)
  // over clock/IP to keep one uncluttered bar.
  const ReaderContext& rc = ReaderContext::get();
  int leftTextW = 0;

  if (rc.active) {
    int progressW = 0;
    if (rc.progress[0] != '\0') {
      progressW = renderer.getTextWidth(SMALL_FONT_ID, rc.progress);
      if (leftTextX + progressW < textRightLimit) {
        renderer.drawText(SMALL_FONT_ID, leftTextX, textY, rc.progress, true);
      }
    }
    leftTextW = progressW;

    if (rc.title[0] != '\0') {
      const int titleLeft = leftTextX + (progressW > 0 ? progressW + kTextGap : 0);
      const int titleSpace = textRightLimit - kTextGap - titleLeft;
      if (titleSpace > 0) {
        std::string fitted = renderer.truncatedText(SMALL_FONT_ID, rc.title, titleSpace);
        const int titleW = renderer.getTextWidth(SMALL_FONT_ID, fitted.c_str());
        const int centeredX = (screenW - titleW) / 2;
        const int titleX = std::max(titleLeft, std::min(centeredX, textRightLimit - kTextGap - titleW));
        renderer.drawText(SMALL_FONT_ID, titleX, textY, fitted.c_str(), true);
      }
    }

    if (rc.progressBarPercent >= 0) {
      const int thickness = std::max(1, rc.progressBarThicknessPx);
      // Draw along the content-facing edge of the band (adjacent to the separator).
      const int progY =
          (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? barY : barY + barH - thickness;
      const int fillW = screenW * std::min(100, rc.progressBarPercent) / 100;
      renderer.fillRect(0, progY, fillW, thickness, true);
    }
  } else {
    const bool showIp = shouldShowIp;
    leftTextW = showIp ? renderer.getTextWidth(SMALL_FONT_ID, "255.255.255.255") : 0;

#if ENABLE_WIFI_CLOCK
    const std::string clockText = DateUtils::currentClockLabel();
    if (!clockText.empty()) {
      const int clockW = renderer.getTextWidth(SMALL_FONT_ID, clockText.c_str());
      const int clockX = (screenW - clockW) / 2;
      if (clockX > leftTextX + leftTextW + kTextGap && clockX + clockW < textRightLimit - kTextGap) {
        renderer.drawText(SMALL_FONT_ID, clockX, textY, clockText.c_str(), true);
      }
    }
#endif

    if (showIp) {
      char ipBuf[22];
      const IPAddress ip = WiFi.localIP();
      snprintf(ipBuf, sizeof(ipBuf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      if (leftTextX + renderer.getTextWidth(SMALL_FONT_ID, ipBuf) < textRightLimit - kTextGap) {
        renderer.drawText(SMALL_FONT_ID, leftTextX, textY, ipBuf, true);
      }
    }
  }

  const int batteryX = screenW - padHPx - metrics.batteryWidth;
  const int batteryY = barY + (barH - metrics.batteryHeight) / 2;
  GUI.drawBatteryRight(renderer, Rect{batteryX, batteryY, metrics.batteryWidth, metrics.batteryHeight},
                       showBatteryPercentage);

  int iconRight = batteryX - kStatusIconGap;
  if (isWifiConnected) {
    iconRight -= kStatusIconSize;
    drawWifiIcon(renderer, iconRight, iconY);
  }
  if (isFileServerRunning) {
    iconRight -= isWifiConnected ? kStatusIconGap + kStatusIconSize : kStatusIconSize;
    drawSyncIcon(renderer, iconRight, iconY);
  }
}

void onSettingsLoaded(GfxRenderer& renderer) { renderer.setPostRenderHook(drawStatusOverlay); }

}  // namespace

#endif  // ENABLE_GLOBAL_STATUS_BAR

// Single shared instance of the reading context the readers publish into.
ReaderContext& ReaderContext::get() {
  static ReaderContext ctx;
  return ctx;
}

// Geometry accessors — always defined (readers/Home/UITheme call topInset()/
// bottomInset() unconditionally). The global bar follows the reader bar's
// polished size + padding, so ThemeMetrics is the single source of truth.
int barHeight() { return UITheme::getInstance().getBaseMetrics().statusBarVerticalMargin; }

int padH() { return UITheme::getInstance().getBaseMetrics().statusBarHorizontalMargin; }

int padTop() {
  const int h = barHeight();
  return h > kStatusIconSize ? (h - kStatusIconSize) / 2 : 0;
}

int topInset() {
  return isEnabled() && SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_TOP ? barHeight() : 0;
}

int bottomInset() {
  return isEnabled() && SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM ? barHeight() : 0;
}

void registerFeature() {
#if ENABLE_GLOBAL_STATUS_BAR
  core::LifecycleEntry entry{};
  entry.onSettingsLoaded = onSettingsLoaded;
  core::LifecycleRegistry::add(entry);
#endif
}

}  // namespace features::status_overlay

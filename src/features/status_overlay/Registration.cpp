#include "features/status_overlay/Registration.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <HalClock.h>
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
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiService.h"
#include "util/DateUtils.h"

namespace features::status_overlay {

namespace {

constexpr int kStatusHorizontalPadBoost = 3;

// The device bezel overlaps the top ~2 px of the panel, so a top-positioned status
// bar loses its first two rows of content. Inset the content and grow the bar by the
// same amount; the bottom position is unaffected (no bezel overlap there).
constexpr int kStatusTopPadBoost = 2;

// Defined here rather than beside the other geometry accessors below because
// drawStatusOverlay() calls it and is defined earlier in this file.
int topPadBoost() {
#if ENABLE_GLOBAL_STATUS_BAR
  return SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_OFF ? 0 : kStatusTopPadBoost;
#else
  return 0;
#endif
}

}  // namespace

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
  if (!activityManager.showsGlobalStatusBar()) {
    return;
  }

  const wifi_mode_t mode = WiFi.getMode();
  const bool isWifiConnected =
      (mode & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  const bool shouldShowIp = isWifiConnected && activityManager.showsStatusBarIp();
  const bool isFileServerRunning = BackgroundWebServer::getInstance().isRunning() || BG_WIFI.isServing();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  constexpr int kTextGap = 12;
  const int barH = barHeight();
  const int padTopPx = textTop(renderer);
  const int padHPx = padH();
  const uint8_t barScope = SETTINGS.globalStatusBarPosition;
  if (barScope == CrossPointSettings::STATUS_BAR_OFF) return;
  // Gate on the ACTIVITY, not on ReaderContext::active. active is set by
  // BaseTheme::drawStatusBar() on the render path, so it is still false while
  // the reader computes its margins via topInset() — gating on it would
  // paginate for a full-height viewport and then draw the bar over the first
  // line, and would make the viewport oscillate (which invalidates the section
  // cache). isReaderActivity() is stable for the whole activity lifetime.
  if (barScope == CrossPointSettings::STATUS_BAR_READER_ONLY && !activityManager.isReaderActivity()) return;
  const int barY = 0;
  const int sepY = barY + barH - 1;
  const int textY = barY + topPadBoost() + padTopPx;

  renderer.fillRect(0, barY, screenW, barH, false);
  renderer.drawLine(0, sepY, screenW - 1, sepY, true);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int batteryGroupWidth =
      metrics.batteryWidth +
      (showBatteryPercentage ? renderer.getTextWidth(SMALL_FONT_ID, "100%") + BaseTheme::batteryPercentSpacing : 0);
  int textRightLimit = screenW - padHPx - batteryGroupWidth;
  const int iconY = itemY(barY, barH, kStatusIconSize);
  if (isWifiConnected) {
    textRightLimit -= kStatusIconSize;
  }
  if (isFileServerRunning) {
    textRightLimit -= isWifiConnected ? kStatusIconGap + kStatusIconSize : kStatusIconSize;
  }
  const int leftTextX = padHPx;
  int contentLeft = leftTextX;

  const ReaderContext& rc = ReaderContext::get();

#if ENABLE_BOOKMARKS
  if (rc.active && rc.pageBookmarked) {
    constexpr int BM_WIDTH = 9;
    constexpr int BM_HEIGHT = 14;
    constexpr int BM_NOTCH_DEPTH = 4;
    const int bmX = leftTextX;
    const int bmY = itemY(barY, barH, BM_HEIGHT);
    const int xPts[5] = {bmX, bmX + BM_WIDTH - 1, bmX + BM_WIDTH - 1, bmX + BM_WIDTH / 2, bmX};
    const int yPts[5] = {bmY, bmY, bmY + BM_HEIGHT - 1, bmY + BM_HEIGHT - 1 - BM_NOTCH_DEPTH, bmY + BM_HEIGHT - 1};
    renderer.fillPolygon(xPts, yPts, 5, true);
    contentLeft = bmX + BM_WIDTH + kStatusIconGap;
  }
#endif

  if (rc.active) {
    int progressW = 0;
    if (rc.progress[0] != '\0') {
      progressW = renderer.getTextWidth(SMALL_FONT_ID, rc.progress);
      if (contentLeft + progressW < textRightLimit) {
        renderer.drawText(SMALL_FONT_ID, contentLeft, textY, rc.progress, true);
      }
    }

    if (rc.title[0] != '\0') {
      const int titleLeft = contentLeft + (progressW > 0 ? progressW + kTextGap : 0);
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
      const int progY = barY + barH - thickness;
      const int fillW = screenW * std::min(100, rc.progressBarPercent) / 100;
      renderer.fillRect(0, progY, fillW, thickness, true);
    }
  } else {
    const bool showIp = shouldShowIp;
    const int leftTextW = showIp ? renderer.getTextWidth(SMALL_FONT_ID, "255.255.255.255") : 0;

    // One overlay clock, time source chosen by hardware: the X3 DS3231 RTC drives
    // a precise digital clock; otherwise the X4 shows the Roman-numeral clock from
    // system time (gated by ENABLE_WIFI_CLOCK).
    std::string clockText;
    if (halClock.isAvailable()) {
      clockText = DateUtils::currentDigitalClockLabel();
    }
#if ENABLE_WIFI_CLOCK
    else {
      clockText = DateUtils::currentClockLabel();
    }
#endif
    if (!clockText.empty()) {
      const int clockW = renderer.getTextWidth(SMALL_FONT_ID, clockText.c_str());
      const int clockX = (screenW - clockW) / 2;
      if (clockX > leftTextX + leftTextW + kTextGap && clockX + clockW < textRightLimit - kTextGap) {
        renderer.drawText(SMALL_FONT_ID, clockX, textY, clockText.c_str(), true);
      }
    }

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
  const int batteryY = itemY(barY, barH, metrics.batteryHeight);
  GUI.drawBatteryRight(renderer, Rect{batteryX, batteryY, metrics.batteryWidth, metrics.batteryHeight},
                       showBatteryPercentage, textY);

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

// Geometry accessors — always defined (readers/Home/UITheme call topInset()
// unconditionally). The global bar follows the reader bar's
// polished size + padding, so ThemeMetrics is the single source of truth.
int barHeight() { return UITheme::getInstance().getBaseMetrics().statusBarVerticalMargin + topPadBoost(); }

int padH() { return UITheme::getInstance().getBaseMetrics().statusBarHorizontalMargin + kStatusHorizontalPadBoost; }

// Pure centring of one SMALL_FONT_ID line within a bar of the theme's nominal
// height. Deliberately excludes topPadBoost(): SleepActivity centres text in a
// band it lays out itself at the BOTTOM of the sleep screen (textBandH =
// statusBarVerticalMargin), so folding the boost in here would shift that text
// down inside a band that never grew — and tie it to an unrelated setting.
// The bezel boost is applied by drawStatusOverlay(), the only caller that
// actually draws the top-positioned bar.
int textTop(const GfxRenderer& renderer) {
  const int usableH = UITheme::getInstance().getBaseMetrics().statusBarVerticalMargin;
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  return usableH > lineH ? (usableH - lineH) / 2 : 0;
}

int itemY(const int barY, const int barH, const int itemH) {
  const int boost = topPadBoost();
  const int usableH = barH - boost;
  return barY + boost + (usableH > itemH ? (usableH - itemH) / 2 : 0);
}

int topInset() {
#if ENABLE_GLOBAL_STATUS_BAR
  const uint8_t scope = SETTINGS.globalStatusBarPosition;
  if (scope == CrossPointSettings::STATUS_BAR_OFF) return 0;
  // Same reasoning as the draw gate: this feeds UITheme::getMetrics() and the
  // readers' margin arithmetic, both of which run before anything sets
  // ReaderContext::active. Must be stable per activity, not per frame.
  if (scope == CrossPointSettings::STATUS_BAR_READER_ONLY && !activityManager.isReaderActivity()) return 0;
  return barHeight();
#else
  return 0;
#endif
}

void registerFeature() {
#if ENABLE_GLOBAL_STATUS_BAR
  core::LifecycleEntry entry{};
  entry.onSettingsLoaded = onSettingsLoaded;
  core::LifecycleRegistry::add(entry);
#endif
}

}  // namespace features::status_overlay

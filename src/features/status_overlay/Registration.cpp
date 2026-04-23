#include "features/status_overlay/Registration.h"

#include <FeatureFlags.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <WiFi.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "core/registries/LifecycleRegistry.h"
#include "fontIds.h"
#include "network/BackgroundWebServer.h"

namespace features::status_overlay {

#if ENABLE_GLOBAL_STATUS_BAR

namespace {

constexpr int kStatusIconSize = 16;
constexpr int kStatusIconGap = 4;

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
  if (!SETTINGS.globalStatusBar) {
    return;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int lineH = renderer.getLineHeight(SMALL_FONT_ID);
  constexpr int kPadV = 2;
  constexpr int kPadH = 4;
  const int barH = std::max(lineH + 2 * kPadV, kStatusIconSize + 2 * kPadV);
  const int barY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? screenH - barH : 0;
  const int sepY = (SETTINGS.globalStatusBarPosition == CrossPointSettings::STATUS_BAR_BOTTOM) ? barY : barY + barH - 1;
  const int textY = barY + (barH - lineH) / 2;

  // White background strip.
  renderer.fillRect(0, barY, screenW, barH, false);
  // 1px separator line.
  renderer.drawLine(0, sepY, screenW - 1, sepY, true);

  // --- Battery (left side) ---
  char batBuf[8];
  snprintf(batBuf, sizeof(batBuf), "%u%%", static_cast<unsigned>(powerManager.getBatteryPercentage()));
  renderer.drawText(SMALL_FONT_ID, kPadH, textY, batBuf, true);

  // --- Status icons (right side) ---
  const wifi_mode_t mode = WiFi.getMode();
  const bool isWifiConnected = (mode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isFileServerRunning = BackgroundWebServer::getInstance().isRunning();
  int iconRight = screenW - kPadH;
  const int iconY = barY + (barH - kStatusIconSize) / 2;

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

void registerFeature() {
#if ENABLE_GLOBAL_STATUS_BAR
  core::LifecycleEntry entry{};
  entry.onSettingsLoaded = onSettingsLoaded;
  core::LifecycleRegistry::add(entry);
#endif
}

}  // namespace features::status_overlay

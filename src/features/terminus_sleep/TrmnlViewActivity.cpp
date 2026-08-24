#include "features/terminus_sleep/TrmnlViewActivity.h"

#if ENABLE_TERMINUS_SLEEP

#include <Bitmap.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <HeapGuard.h>
#include <I18n.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <SpiBusMutex.h>

#include <algorithm>
#include <iterator>

#include "components/UITheme.h"
#include "features/terminus_sleep/Registration.h"
#include "fontIds.h"
#include "network/background/BackgroundWebServer.h"
#include "network/background/BackgroundWifiCoordinator.h"
#include "network/background/BackgroundWifiService.h"
#include "network/wifi/WifiUtil.h"

namespace {

// The fetch pins the dashboard under the extension matching its sniffed content
// type, and deletes every other variant, so at most one of these exists.
constexpr const char* kDashboardPaths[] = {
    "/sleep/trmnl_latest.bmp",
    "/sleep/trmnl_latest.png",
    "/sleep/trmnl_latest.jpg",
};

// Awake cap for the blocking refresh. Shorter than the 60 s sleep-path cap:
// here a user is watching a frozen screen, so a stalled fetch must give up
// sooner. The fetch itself runs on its own task; this only bounds the wait.
constexpr uint32_t kFetchCapMs = 45000;
constexpr uint32_t kWifiWaitMs = 20000;

// Scratch BMP the PNG repack writes to. Its own name rather than a sibling of
// the source, so it can never collide with or go stale against a user file; it
// is rewritten before every read.
constexpr char kRepackBmp[] = "/sleep/.trmnl_view.bmp";

bool hasExtension(const std::string& path, const char* ext) {
  return path.size() >= 4 && path.compare(path.size() - 4, 4, ext) == 0;
}

// Render a wide dashboard in the panel's native landscape for one draw, then put
// the orientation back.
//
// The panel is natively 800x480 but the UI runs portrait, so getScreenWidth() is
// 480. A Terminus dashboard is exactly 800x480, so drawn portrait it scaled to
// 0.6 and sat in a band across the top third of the screen — verified on device.
// In LandscapeCounterClockwise ("native panel orientation", GfxRenderer.h:38) the
// same image fills the panel 1:1.
//
// Applied only when the image is wider than tall AND the UI is portrait, so it
// is strictly an improvement and crops nothing that was not cropped before.
// RAII because a leaked orientation would rotate the whole UI.
//
// Deliberately a local copy of SleepActivity.cpp's SleepLandscapeScope: that one
// is file-local, and hoisting it into a shared header would mean editing the
// sleep render path, which is the most crash-sensitive code on the device and
// outside the scope of this change.
class LandscapeScope {
 public:
  LandscapeScope(GfxRenderer& renderer, const int imageWidth, const int imageHeight) : renderer_(renderer) {
    const GfxRenderer::Orientation current = renderer.getOrientation();
    const bool uiIsPortrait = current == GfxRenderer::Portrait || current == GfxRenderer::PortraitInverted;
    if (imageWidth > imageHeight && uiIsPortrait) {
      previous_ = current;
      renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
      active_ = true;
    }
  }
  ~LandscapeScope() {
    if (active_) renderer_.setOrientation(previous_);
  }
  LandscapeScope(const LandscapeScope&) = delete;
  LandscapeScope& operator=(const LandscapeScope&) = delete;

 private:
  GfxRenderer& renderer_;
  GfxRenderer::Orientation previous_ = GfxRenderer::Portrait;
  bool active_ = false;
};

}  // namespace

void TrmnlViewActivity::onEnter() {
  Activity::onEnter();
  resolveCachedPath();
  // The repack itself happens in render() — see prepareRenderable's contract.
  repackPending = true;
  status = cachedPath.empty() ? Status::Empty : Status::Showing;
  requestUpdate();
}

void TrmnlViewActivity::prepareRenderable() {
  renderPath.clear();
  if (cachedPath.empty()) {
    return;
  }
  // BMP and JPEG stream cheaply and can be drawn straight from the source.
  if (!hasExtension(cachedPath, ".png")) {
    renderPath = cachedPath;
    return;
  }

  // PNG cannot go through PNGdec here. PNGdec is one 58,912-byte struct and the
  // X4's largest free block while awake is ~38,900 — measured; the first
  // on-device run of this screen failed exactly here with "Failed to decode
  // dashboard". PngToBmpConverter streams the same image through miniz, whose
  // ~43 KB of state and window are claimed from the lent framebuffer rather
  // than the heap, so it does not need a large heap run at all.
  //
  // The loan nulls the framebuffer, so nothing may draw while it is held. That
  // is why this runs under render()'s RenderLock — see the header contract.
  bool ok = false;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    SpiBusMutex::Guard guard;
    HalFile pngFile;
    HalFile bmpFile;
    if (Storage.openFileForRead("TRMNL", cachedPath, pngFile) &&
        Storage.openFileForWrite("TRMNL", kRepackBmp, bmpFile)) {
      ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(pngFile, bmpFile, HalDisplay::DISPLAY_WIDTH,
                                                             HalDisplay::DISPLAY_HEIGHT, false);
    }
    pngFile.close();
    bmpFile.close();
  }

  if (!ok) {
    LOG_ERR("TRMNL", "PNG repack failed for %s (free=%u largest=%u)", cachedPath.c_str(),
            static_cast<unsigned>(heapguard::freeBytes()), static_cast<unsigned>(heapguard::largestBlock()));
    SpiBusMutex::Guard guard;
    if (Storage.exists(kRepackBmp)) Storage.remove(kRepackBmp);
    return;
  }
  LOG_INF("TRMNL", "Dashboard repacked: %s -> %s", cachedPath.c_str(), kRepackBmp);
  renderPath = kRepackBmp;
}

void TrmnlViewActivity::resolveCachedPath() {
  SpiBusMutex::Guard guard;
  const auto end = std::end(kDashboardPaths);
  const auto found =
      std::find_if(std::begin(kDashboardPaths), end, [](const char* path) { return Storage.exists(path); });
  if (found != end) {
    cachedPath = *found;
    return;
  }
  cachedPath.clear();
}

void TrmnlViewActivity::loop() {
  // Paint the progress screen first, then block: requestUpdateAndWait() in
  // loop() would run the render before this returns, but doing the fetch here
  // (rather than in render()) keeps the blocking call off the render task.
  if (refreshPending) {
    refreshPending = false;
    performRefresh();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    status = Status::Refreshing;
    refreshPending = true;
    requestUpdateAndWait();
  }
}

void TrmnlViewActivity::performRefresh() {
  // The 12 KB fetch task needs contiguous heap that a running web server does
  // not leave behind — the same constraint the sleep path works around. Keep
  // WiFi up so the reconnect below is usually a no-op.
  if (BG_WIFI.isRunning()) BG_WIFI.stop(/*keepWifi=*/true);
  if (BackgroundWebServer::getInstance().isRunning()) BackgroundWebServer::getInstance().stop(/*keepWifi=*/true);

  auto& wifiCoord = BackgroundWifiCoordinator::getInstance();
  const bool alreadyConnected = hasStaWifiConnection();
  const bool connectStarted = alreadyConnected || wifiCoord.beginTimedSleepAutoConnect("TRMNL");
  if (!connectStarted || (!alreadyConnected && !wifiCoord.waitForStaConnection(kWifiWaitMs))) {
    LOG_WRN("TRMNL", "No WiFi for foreground refresh");
    if (!alreadyConnected) wifiCoord.endTimedSleepWifi();
    status = Status::NoWifi;
    return;
  }

  const bool ok = features::terminus_sleep::startTrmnlFetchAndWait(kFetchCapMs);
  if (!alreadyConnected) wifiCoord.endTimedSleepWifi();

  resolveCachedPath();
  repackPending = true;
  if (ok) {
    status = Status::Showing;
    return;
  }
  LOG_WRN("TRMNL", "Foreground refresh failed");
  // RefreshFailed even with nothing cached: reporting plain "Empty" here would
  // repaint the identical pre-Refresh screen, so a failed refresh would be
  // indistinguishable from the button not working at all.
  status = Status::RefreshFailed;
}

// Draws AND displays, because the landscape scope must still be alive when the
// buffer is pushed to the panel — restoring the orientation first would show the
// image rotated.
bool TrmnlViewActivity::renderDashboard(const char* failure) {
  if (hasExtension(renderPath, ".bmp")) {
    SpiBusMutex::Guard guard;
    HalFile file;
    if (!Storage.openFileForRead("TRMNL", renderPath, file)) {
      LOG_ERR("TRMNL", "Cannot open dashboard: %s", renderPath.c_str());
      return false;
    }
    // No dithering: a TRMNL dashboard is already 1-bit line art and text, which
    // dithering only smears.
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      LOG_ERR("TRMNL", "Dashboard BMP headers unreadable");
      return false;
    }
    LandscapeScope landscape(renderer, bitmap.getWidth(), bitmap.getHeight());
    renderer.clearScreen();
    renderer.drawBitmap(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
    drawFailureBanner(failure);
    renderer.displayBuffer();
    return true;
  }

  // JPEG only — PNG never reaches here, prepareRenderable() has already turned
  // it into a BMP. JPEGDEC's working set is a few KB, not PNGdec's ~59, so it is
  // viable while awake.
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(renderPath);
  if (!decoder) {
    LOG_ERR("TRMNL", "No decoder for dashboard: %s", renderPath.c_str());
    return false;
  }

  ImageDimensions dims = {0, 0};
  if (!decoder->getDimensions(renderPath, dims) || dims.width <= 0 || dims.height <= 0) {
    LOG_ERR("TRMNL", "No dimensions for dashboard: %s", renderPath.c_str());
    return false;
  }

  LandscapeScope landscape(renderer, dims.width, dims.height);
  renderer.clearScreen();

  RenderConfig config;
  config.x = 0;
  config.y = 0;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  // Single BW pass: grayscale would mean three full decodes of artwork that is
  // effectively monochrome.
  config.useGrayscale = false;
  config.useDithering = false;

  if (!decoder->decodeToFramebuffer(renderPath, renderer, config)) {
    LOG_ERR("TRMNL", "Failed to decode dashboard: %s", renderPath.c_str());
    return false;
  }
  drawFailureBanner(failure);
  renderer.displayBuffer();
  return true;
}

// Never show a stale dashboard as if it were fresh: this banner is the only
// signal the user gets that Refresh did not land. No-op when nothing failed.
void TrmnlViewActivity::drawFailureBanner(const char* failure) const {
  if (failure == nullptr) {
    return;
  }
  const int bannerHeight = UITheme::getInstance().getMetrics().headerHeight;
  const int bannerTop = renderer.getScreenHeight() - bannerHeight;
  renderer.fillRect(0, bannerTop, renderer.getScreenWidth(), bannerHeight, true);
  renderer.drawCenteredText(UI_12_FONT_ID, bannerTop + bannerHeight / 4, failure, false);
}

void TrmnlViewActivity::renderMessage(const char* message) const {
  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "TRMNL");
  renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, message);
}

void TrmnlViewActivity::render(RenderLock&&) {
  // Safe here and nowhere else: RenderLock is held, so the render task cannot
  // re-enter and draw against the nulled framebuffer the repack borrows.
  if (repackPending) {
    repackPending = false;
    prepareRenderable();
  }

  if (status == Status::Refreshing) {
    renderMessage(tr(STR_TRMNL_REFRESHING));
    renderer.displayBuffer();
    return;
  }

  const char* failure = nullptr;
  if (status == Status::NoWifi) {
    failure = tr(STR_TRMNL_NO_WIFI);
  } else if (status == Status::RefreshFailed) {
    failure = tr(STR_TRMNL_REFRESH_FAILED);
  }

  // Full-bleed on success: hints would sit on top of dashboard content, and the
  // dashboard is the whole point of the screen. renderDashboard() displays for
  // itself so the landscape orientation is still in effect at push time.
  if (!renderPath.empty() && renderDashboard(failure)) {
    return;
  }

  renderMessage(failure != nullptr ? failure : tr(STR_TRMNL_NO_IMAGE));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TRMNL_REFRESH), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif

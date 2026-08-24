#pragma once
#include <FeatureFlags.h>

#if ENABLE_TERMINUS_SLEEP

#include <string>

#include "activities/Activity.h"

// Foreground view of the Terminus/TRMNL dashboard.
//
// The dashboard already lands on the SD card as /sleep/trmnl_latest.<ext>, put
// there by the background fetch in features/terminus_sleep. Until now it was
// only ever rendered by SleepActivity, so the dashboard was unreachable while
// the device was awake. This activity adds the missing foreground render and
// reuses the existing, on-device-proven fetch verbatim for its Refresh action —
// no new network code.
class TrmnlViewActivity final : public Activity {
 public:
  TrmnlViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Trmnl", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

  // The dashboard is drawn full-bleed at 800x480; a status bar would sit on top
  // of live data.
  bool showsGlobalStatusBar() const override { return false; }

  // Measured on device: the background web server coming up mid-decode dropped
  // the largest free block below what image decoding needs, and the dashboard
  // failed to render. This screen is short-lived and explicitly foreground.
  bool blocksBackgroundServer() override { return true; }

 private:
  enum class Status : uint8_t {
    Showing,        // cachedPath holds a renderable dashboard
    Empty,          // nothing fetched yet
    Refreshing,     // blocking fetch in progress; render the progress screen
    RefreshFailed,  // fetch failed, cachedPath (if any) is stale but still shown
    NoWifi,
  };

  // Source image as fetched (.bmp/.png/.jpg).
  std::string cachedPath;
  // What render() actually draws. For BMP and JPEG this is cachedPath; for PNG
  // it is the repacked scratch BMP, because PNGdec cannot be instantiated while
  // the device is awake (see prepareRenderable).
  std::string renderPath;
  Status status = Status::Empty;
  // Set while a refresh runs so loop() performs the blocking fetch AFTER the
  // progress screen has actually been painted.
  bool refreshPending = false;
  // Source changed; render() must (re)derive renderPath before drawing.
  bool repackPending = false;

  // Locates the newest /sleep/trmnl_latest.<ext> on the SD card, or clears
  // cachedPath when none exists.
  void resolveCachedPath();
  // Produces something render() can stream, repacking PNG to BMP if needed.
  //
  // MUST be called only from render(). The repack takes a FrameBufferLoan, which
  // nulls the framebuffer, so anything that draws while it is held dereferences
  // null. render() is invoked by renderTaskLoop() while it holds RenderLock, so
  // the render task cannot re-enter and draw — the same "safe by construction"
  // placement EpubReaderActivity uses. Calling this from onEnter() or loop()
  // would hold the loan with no RenderLock; see docs/FINDINGS.md.
  void prepareRenderable();
  void performRefresh();
  // Draws and displays the dashboard; `failure` (may be null) is overlaid as a
  // banner so a stale image is never mistaken for a fresh one.
  bool renderDashboard(const char* failure);
  void drawFailureBanner(const char* failure) const;
  void renderMessage(const char* message) const;
};

#endif

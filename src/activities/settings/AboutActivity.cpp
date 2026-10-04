#include "AboutActivity.h"

#include <AppVersion.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;
namespace {
enum Row {
  Device,
  Firmware,
  Build,
  Chip,
  Cpu,
  Flash,
  Display,
  Resolution,
  Touch,
  FrontlightRow,
  Rtc,
  ImuRow,
  SdTransport,
  SdCapacity,
  Psram,
  InternalFree,
  InternalMinimum,
  InternalLargest,
  PsramFree,
  PsramLargest,
  Uptime,
  Reset,
  Sdk,
  RowCount
};
constexpr StrId labels[] = {StrId::STR_ABOUT_DEVICE,
                            StrId::STR_ABOUT_FIRMWARE,
                            StrId::STR_ABOUT_BUILD,
                            StrId::STR_ABOUT_CHIP,
                            StrId::STR_ABOUT_CPU,
                            StrId::STR_ABOUT_FLASH,
                            StrId::STR_ABOUT_DISPLAY,
                            StrId::STR_ABOUT_RESOLUTION,
                            StrId::STR_ABOUT_TOUCH,
                            StrId::STR_FRONTLIGHT,
                            StrId::STR_ABOUT_RTC,
                            StrId::STR_ABOUT_IMU,
                            StrId::STR_ABOUT_SD_TRANSPORT,
                            StrId::STR_ABOUT_SD_CAPACITY,
                            StrId::STR_ABOUT_PSRAM,
                            StrId::STR_ABOUT_INTERNAL_FREE,
                            StrId::STR_ABOUT_INTERNAL_MINIMUM,
                            StrId::STR_ABOUT_INTERNAL_LARGEST,
                            StrId::STR_ABOUT_PSRAM_FREE,
                            StrId::STR_ABOUT_PSRAM_LARGEST,
                            StrId::STR_ABOUT_UPTIME,
                            StrId::STR_ABOUT_RESET,
                            StrId::STR_ABOUT_SDK};
static_assert(sizeof(labels) / sizeof(labels[0]) == RowCount);
const char* presenceText(HalDeviceInfo::Presence value) {
  switch (value) {
    case HalDeviceInfo::Presence::Absent:
      return tr(STR_ABOUT_NOT_PRESENT);
    case HalDeviceInfo::Presence::Available:
      return tr(STR_ABOUT_AVAILABLE);
    case HalDeviceInfo::Presence::Unavailable:
      return tr(STR_ABOUT_UNAVAILABLE);
    case HalDeviceInfo::Presence::Simulated:
      return tr(STR_ABOUT_SIMULATED);
  }
  return tr(STR_ABOUT_UNAVAILABLE);
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("About", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void AboutActivity::onEnter() {
  Activity::onEnter();
  snapshot = HalDeviceInfo::capture();
  LOG_DBG("ABOUT", "Activity allocation: %u bytes; snapshot: %u bytes", static_cast<unsigned>(sizeof(*this)),
          static_cast<unsigned>(sizeof(snapshot)));
  applySharedUiTheme(app, uiTarget);
  app.setScreen(&AboutActivity::aboutScreen, this);
  requestUpdate();
}

void AboutActivity::loop() {
  RenderLock lock(*this);  // Protect viewport state shared with the render task.
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }
  const auto scroll = [this](int delta) {
    const int next = scrollListBy(topIndex, delta, visibleRows, RowCount);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
  };
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    scroll(swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows);
    return;
  }
  buttonNavigator.onNext([&] { scroll(visibleRows); });
  buttonNavigator.onPrevious([&] { scroll(-visibleRows); });
}

void AboutActivity::provideRow(void* user, uint16_t index, fui::ListItem& item) {
  auto& self = *static_cast<AboutActivity*>(user);
  const auto& s = self.snapshot;
  auto* buf = self.valueBuffer;
  const auto size = sizeof(self.valueBuffer);
  item.label = I18N.get(labels[index]);
  item.subtitle = tr(STR_ABOUT_UNSUPPORTED);
  auto kib = [&](uint32_t bytes) {
    snprintf(buf, size, "%lu KiB", static_cast<unsigned long>(bytes / 1024));
    item.subtitle = buf;
  };
  switch (index) {
    case Device:
      item.subtitle = s.device;
      break;
    case Firmware:
      item.subtitle = AppVersion::version();
      break;
    case Build:
      snprintf(buf, size, "%s / %s / %s", CROSSINK_FIRMWARE_DEVICE_TYPE, AppVersion::gitSha(),
               AppVersion::gitDirtyFlag()[0] == '1'   ? tr(STR_ABOUT_MODIFIED)
               : AppVersion::gitDirtyFlag()[0] == '0' ? tr(STR_ABOUT_CLEAN)
                                                      : tr(STR_ABOUT_UNAVAILABLE));
      item.subtitle = buf;
      break;
    case Chip:
      if (!s.simulated) {
        snprintf(buf, size, "%s / %u", s.chip, s.chipRevision);
        item.subtitle = buf;
      }
      break;
    case Cpu:
      if (!s.simulated) {
        snprintf(buf, size, tr(STR_ABOUT_CPU_FORMAT), s.cores, s.cpuMHz);
        item.subtitle = buf;
      }
      break;
    case Flash:
      if (!s.simulated) kib(s.flashBytes);
      break;
    case Display:
      if (s.displayController) item.subtitle = s.displayController;
      break;
    case Resolution:
      snprintf(buf, size, "%u x %u", s.width, s.height);
      item.subtitle = buf;
      break;
    case Touch:
      item.subtitle = presenceText(s.touch);
      if (s.touchController) {
        snprintf(buf, size, "%s / %s", s.touchController, item.subtitle);
        item.subtitle = buf;
      }
      break;
    case FrontlightRow:
      item.subtitle = presenceText(s.frontlight);
      break;
    case Rtc:
      item.subtitle = presenceText(s.rtc);
      break;
    case ImuRow:
      item.subtitle = presenceText(s.imu);
      break;
    case SdTransport:
      item.subtitle = s.simulated ? tr(STR_ABOUT_SIMULATED) : s.sdmmc ? "SDMMC" : "SPI";
      break;
    case SdCapacity:
      if (s.simulated && s.sdReady)
        item.subtitle = tr(STR_ABOUT_SIMULATED);
      else if (!s.sdReady || !s.sdBytes)
        item.subtitle = tr(STR_ABOUT_UNAVAILABLE);
      else {
        snprintf(buf, size, "%llu MiB", static_cast<unsigned long long>(s.sdBytes / (1024 * 1024)));
        item.subtitle = buf;
      }
      break;
    case Psram:
      if (!s.simulated) {
        if (s.psramTotal)
          kib(s.psramTotal);
        else
          item.subtitle = presenceText(s.psram);
      }
      break;
    case InternalFree:
      if (!s.simulated) kib(s.internalFree);
      break;
    case InternalMinimum:
      if (!s.simulated) kib(s.internalMinimum);
      break;
    case InternalLargest:
      if (!s.simulated) kib(s.internalLargest);
      break;
    case PsramFree:
      if (!s.simulated) {
        if (s.psramTotal)
          kib(s.psramFree);
        else
          item.subtitle = presenceText(s.psram);
      }
      break;
    case PsramLargest:
      if (!s.simulated) {
        if (s.psramTotal)
          kib(s.psramLargest);
        else
          item.subtitle = presenceText(s.psram);
      }
      break;
    case Uptime:
      snprintf(buf, size, tr(STR_ABOUT_UPTIME_FORMAT), static_cast<unsigned long>(s.uptimeSeconds));
      item.subtitle = buf;
      break;
    case Reset:
      if (!s.simulated) {
        snprintf(buf, size, "%u", s.resetReason);
        item.subtitle = buf;
      }
      break;
    case Sdk:
      if (s.sdk) item.subtitle = s.sdk;
      break;
  }
}

void AboutActivity::aboutScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<AboutActivity*>(user);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = TouchHeaderBackButton::headerRect(self.renderer, self.mappedInput);
  const auto safe = screen.frame().safeRect();
  const Rect hints =
      UITheme::getInstance().getScreenSafeArea(self.renderer, !self.mappedInput.hasTouchHardware(), false);
  const int contentTop = std::max({static_cast<int>(safe.y), header.y + header.height, hints.y});
  // Hint strips rotate with the physical buttons; intersect them with bezel-safe bounds.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(contentTop - safe.y),
                                      static_cast<int16_t>(std::max<int>(0, safe.right() - (hints.x + hints.width))),
                                      static_cast<int16_t>(std::max<int>(0, safe.bottom() - (hints.y + hints.height))),
                                      static_cast<int16_t>(std::max<int>(0, hints.x - safe.x))});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.count = RowCount;
  props.rowProvider = &AboutActivity::provideRow;
  props.rowProviderCtx = &self;
  props.inputMask = fui::InputNone;
  self.visibleRows =
      std::max<int>(1, configureUiList(props, screen.theme(), screen.body(), UiListRowType::WithSubtitle));
  self.topIndex = scrollListBy(self.topIndex, 0, self.visibleRows, RowCount);
  props.topIndex = self.topIndex;
  screen.list(props);
}

void AboutActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware())
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_ABOUT), false);
  else
    GUI.drawHeader(renderer, header, tr(STR_ABOUT));
  app.render();
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#ifdef SIMULATOR
int AboutActivity::simulatorRowCount() const { return RowCount; }
#endif

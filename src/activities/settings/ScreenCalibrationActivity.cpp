#include "ScreenCalibrationActivity.h"

#include <HalScreenCalibration.h>
#include <I18n.h>

#include <cstdio>

#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;
namespace {
constexpr int SAFE_INSET = ScreenInsets::MAX_INSET + 16;
constexpr int ROW_HEIGHT = 48;
constexpr int ROW_COUNT = 7;
constexpr int ADJUST_WIDTH = 48;
constexpr fui::ActionId ACTION_CONTROL = 1;
constexpr StrId ROW_LABELS[] = {StrId::STR_TOP,       StrId::STR_RIGHT_EDGE,        StrId::STR_BOTTOM,
                                StrId::STR_LEFT_EDGE, StrId::STR_CALIBRATION_RESET, StrId::STR_SAVE,
                                StrId::STR_CANCEL};
}  // namespace

ScreenCalibrationActivity::ScreenCalibrationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ScreenCalibration", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void ScreenCalibrationActivity::onEnter() {
  Activity::onEnter();
  RenderLock lock(*this);
  draft = renderer.getViewableInsets();
  previousOrientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Portrait);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL, UI_10_FONT_ID);
  auto device = uiTarget.deviceContext();
  // Controls stay reachable even if the candidate border is behind the bezel.
  device.safeArea = fui::Insets{SAFE_INSET, SAFE_INSET, SAFE_INSET, SAFE_INSET};
  app.setDevice(device);
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_CONTROL, &ScreenCalibrationActivity::onControl, this);
  app.setScreen(&ScreenCalibrationActivity::calibrationScreen, this);
  LOG_DBG("CAL", "Calibration activity allocation: %u bytes", static_cast<unsigned>(sizeof(*this)));
  requestUpdate();
}

void ScreenCalibrationActivity::onExit() {
  // ActivityManager holds RenderLock while exiting an activity.
  renderer.setOrientation(previousOrientation);
  Activity::onExit();
}

void ScreenCalibrationActivity::activate() {
  if (selected < 4) {
    editing = !editing;
  } else if (selected == 4) {
    draft = ScreenInsets{};
    saveFailed = false;
  } else if (selected == 5) {
    if (draft == renderer.getViewableInsets() || HalScreenCalibration::save(draft)) {
      renderer.setViewableInsets(draft);
      finish();
      return;
    }
    saveFailed = true;
  } else {
    finish();  // Working copy is discarded.
    return;
  }
  requestUpdate();
}

void ScreenCalibrationActivity::navigate(const int delta) {
  if (editing && selected < 4) {
    draft.adjust(static_cast<unsigned>(selected), delta);
    saveFailed = false;
  } else {
    selected = (selected + delta + ROW_COUNT) % ROW_COUNT;
  }
  requestUpdate();
}

void ScreenCalibrationActivity::onControl(const fui::ActionEvent& event, void* user) {
  auto& self = *static_cast<ScreenCalibrationActivity*>(user);
  // 0..6 rows, 7..10 decrease controls, 11..14 increase controls.
  if (event.value >= 7) {
    self.selected = (event.value - 7) % 4;
    self.editing = true;
    self.navigate(event.value < 11 ? -1 : 1);
  } else {
    self.selected = event.value;
    self.editing = false;
    self.activate();
  }
}

void ScreenCalibrationActivity::loop() {
  RenderLock lock(*this);  // Draft, hit regions, and orientation share the render task.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (editing) {
      editing = false;
      requestUpdate();
    } else {
      finishAfterBackPress();
    }
    return;
  }
  if (uiReady) {
    const auto input = touchSnapshotFrom(mappedInput);
    if (input.touchPressed || input.touchReleased) {
      const auto event = app.route(input);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    // Settings activates rows on release; do not let Save/Cancel reopen us.
    if (selected >= 5) mappedInput.suppressNextConfirmRelease();
    activate();
    return;
  }
  navigator.onNextRelease([this] { navigate(1); });
  navigator.onPreviousRelease([this] { navigate(-1); });
  navigator.onNextContinuous([this] { navigate(1); });
  navigator.onPreviousContinuous([this] { navigate(-1); });
}

void ScreenCalibrationActivity::calibrationScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<ScreenCalibrationActivity*>(user);
  const auto safe = screen.device().safeRect();
  const int panelY = (screen.device().height - ROW_HEIGHT * ROW_COUNT) / 2;
  auto text = screen.theme().smallText;
  text.maxLines = 4;
  const auto instructionRect =
      fui::Rect{safe.x, static_cast<int16_t>(safe.y + 44), safe.width, static_cast<int16_t>(panelY - safe.y - 52)};
  fui::layoutText(
      screen.target(), instructionRect, tr(STR_CALIBRATION_HELP), text,
      [&](const char* line, const fui::Rect rect) { self.renderer.drawText(UI_10_FONT_ID, rect.x, rect.y, line); });
  for (int row = 0; row < ROW_COUNT; ++row) {
    const int y = panelY + row * ROW_HEIGHT;
    const auto bounds = fui::Rect{safe.x, static_cast<int16_t>(y), safe.width, ROW_HEIGHT};
    const bool selected = self.selected == row;
    if (selected) self.renderer.fillRectDither(bounds.x, bounds.y, bounds.width, bounds.height, Color::LightGray);
    self.renderer.drawRect(bounds.x, bounds.y, bounds.width, bounds.height);
    const int textY = y + (ROW_HEIGHT - self.renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    const int edgeWidth = row < 4 ? ADJUST_WIDTH * 2 : 0;
    const auto center =
        bounds.inset(fui::Insets{0, static_cast<int16_t>(edgeWidth / 2), 0, static_cast<int16_t>(edgeWidth / 2)});
    screen.frame().hit(center, ACTION_CONTROL, static_cast<int16_t>(row), fui::InputTouch);
    self.renderer.drawText(UI_10_FONT_ID, center.x + 8, textY, I18N.get(ROW_LABELS[row]));
    if (row < 4) {
      char value[8];
      std::snprintf(value, sizeof(value), "%u", self.draft.edges[row]);
      const int valueWidth = self.renderer.getTextWidth(UI_10_FONT_ID, value);
      self.renderer.drawText(UI_10_FONT_ID, center.right() - valueWidth - 8, textY, value, true,
                             self.editing && selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      const fui::Rect decrease{bounds.x, bounds.y, ADJUST_WIDTH, bounds.height};
      const fui::Rect increase{static_cast<int16_t>(bounds.right() - ADJUST_WIDTH), bounds.y, ADJUST_WIDTH,
                               bounds.height};
      screen.frame().hit(decrease, ACTION_CONTROL, static_cast<int16_t>(7 + row), fui::InputTouch);
      screen.frame().hit(increase, ACTION_CONTROL, static_cast<int16_t>(11 + row), fui::InputTouch);
      self.renderer.drawText(UI_10_FONT_ID, decrease.x + 18, textY, "-");
      self.renderer.drawText(UI_10_FONT_ID, increase.x + 18, textY, "+");
    }
  }
  const auto note = self.saveFailed ? tr(STR_CALIBRATION_SAVE_FAILED)
                                    : (self.editing ? tr(STR_CALIBRATION_EDIT_HELP) : tr(STR_CALIBRATION_SELECT_HELP));
  const auto noteRect = fui::Rect{safe.x, static_cast<int16_t>(panelY + ROW_HEIGHT * ROW_COUNT + 16), safe.width,
                                  static_cast<int16_t>(safe.bottom() - panelY - ROW_HEIGHT * ROW_COUNT - 16)};
  fui::layoutText(screen.target(), noteRect, note, text, [&](const char* line, const fui::Rect rect) {
    self.renderer.drawText(UI_10_FONT_ID, rect.x, rect.y, line);
  });
}

void ScreenCalibrationActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderer.drawRect(draft.edges[3], draft.edges[0], renderer.getScreenWidth() - draft.edges[3] - draft.edges[1],
                    renderer.getScreenHeight() - draft.edges[0] - draft.edges[2]);
  if (selected < 4) {
    const int left = draft.edges[3];
    const int top = draft.edges[0];
    const int right = renderer.getScreenWidth() - draft.edges[1] - 1;
    const int bottom = renderer.getScreenHeight() - draft.edges[2] - 1;
    if (selected == 0 || selected == 2) {
      const int y = selected == 0 ? top : bottom - 2;
      renderer.fillRect(left, y, right - left + 1, 3);
    } else {
      const int x = selected == 3 ? left : right - 2;
      renderer.fillRect(x, top, 3, bottom - top + 1);
    }
  }
  renderer.drawCenteredText(UI_10_FONT_ID, SAFE_INSET, tr(STR_SCREEN_CALIBRATION), true, EpdFontFamily::BOLD);
  uiReady = false;
  app.render();
  uiReady = true;
  renderer.displayBuffer();
}

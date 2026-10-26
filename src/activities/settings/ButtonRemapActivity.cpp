#include "ButtonRemapActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"

namespace {
// UI steps correspond to logical roles in order: Back, Confirm, Left, Right.
constexpr uint8_t kRoleCount = 4;
// Marker used when a role has not been assigned yet.
constexpr uint8_t kUnassigned = 0xFF;
}  // namespace

void ButtonRemapActivity::onEnter() {
  Activity::onEnter();

  // Start with all roles unassigned to avoid duplicate blocking.
  currentStep = 0;
  tempMapping[0] = kUnassigned;
  tempMapping[1] = kUnassigned;
  tempMapping[2] = kUnassigned;
  tempMapping[3] = kUnassigned;
  errorMessage.clear();
  requestUpdate();
}

void ButtonRemapActivity::onExit() { Activity::onExit(); }

void ButtonRemapActivity::loop() {
  // Side buttons:
  // - Up: reset mapping to defaults and exit.
  // - Down: cancel without saving.
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    // Persist default mapping immediately so the user can recover quickly.
    SETTINGS.frontButtonLayout = CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT;
    SETTINGS.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
    SETTINGS.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
    SETTINGS.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
    SETTINGS.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
    SETTINGS.enforceButtonLayoutConstraints();
    const bool saved = SETTINGS.saveToFile();
    if (!saved) {
      LOG_ERR("SET", "Failed to save default front button mapping");
    }
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    // Exit without changing settings.
    finish();
    return;
  }

  {
    // Make sure UI done rendering before accepting another assignment.
    // This avoids rapid double-presses that can advance the step without a visible redraw.
    RenderLock lock(*this);

    // Wait for a front button press to assign to the current role.
    const int pressedButton = mappedInput.getPressedFrontButton();
    if (pressedButton < 0) {
      return;
    }

    // Update temporary mapping and advance the remap step.
    // Only accept the press if this hardware button isn't already assigned elsewhere.
    if (!validateUnassigned(static_cast<uint8_t>(pressedButton))) {
      requestUpdate();
      return;
    }
    tempMapping[currentStep] = static_cast<uint8_t>(pressedButton);
    currentStep++;

    if (currentStep >= kRoleCount) {
      // All roles assigned; save to settings and exit.
      applyTempMapping();
      const bool saved = SETTINGS.saveToFile();
      if (!saved) {
        LOG_ERR("SET", "Failed to save front button mapping");
      }
      finish();
      return;
    }

    requestUpdate();
  }
}

void ButtonRemapActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto labelForHardware = [&](uint8_t hardwareIndex) -> const char* {
    for (uint8_t i = 0; i < kRoleCount; i++) {
      if (tempMapping[i] == hardwareIndex) {
        return getRoleName(i);
      }
    }
    return "-";
  };

  const int top = features::status_overlay::topInset();
  renderer.drawCenteredText(UI_12_FONT_ID, 15 + top, tr(STR_REMAP_FRONT_BUTTONS), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, 40 + top, tr(STR_REMAP_PROMPT));

  for (uint8_t i = 0; i < kRoleCount; i++) {
    const int y = 70 + top + i * 30;
    const bool isSelected = (i == currentStep);

    // Highlight the role that is currently being assigned.
    if (isSelected) {
      renderer.fillRect(0, y - 2, pageWidth - 1, 30);
    }

    const char* roleName = getRoleName(i);
    renderer.drawText(UI_10_FONT_ID, 20, y, roleName, !isSelected);

    // Show currently assigned hardware button (or unassigned).
    const char* assigned = (tempMapping[i] == kUnassigned) ? "Unassigned" : getHardwareName(tempMapping[i]);
    const auto width = renderer.getTextWidth(UI_10_FONT_ID, assigned);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 20 - width, y, assigned, !isSelected);
  }

  // Temporary warning banner for duplicates.
  if (!errorMessage.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, 210 + top, errorMessage.c_str(), true);
  }

  // Provide side button actions at the bottom of the screen (split across two lines).
  renderer.drawCenteredText(SMALL_FONT_ID, 250 + top, tr(STR_REMAP_RESET_HINT), true);
  renderer.drawCenteredText(SMALL_FONT_ID, 280 + top, tr(STR_REMAP_CANCEL_HINT), true);

  // Live preview of logical labels under front buttons.
  // This mirrors the on-device front button order: Back, Confirm, Left, Right.
  GUI.drawButtonHints(renderer, labelForHardware(CrossPointSettings::FRONT_HW_BACK),
                      labelForHardware(CrossPointSettings::FRONT_HW_CONFIRM),
                      labelForHardware(CrossPointSettings::FRONT_HW_LEFT),
                      labelForHardware(CrossPointSettings::FRONT_HW_RIGHT));
  renderer.displayBuffer();
}

void ButtonRemapActivity::applyTempMapping() {
  // Commit temporary mapping into settings (logical role -> hardware).
  // Custom remapping exits any preset-only front layout mode.
  SETTINGS.frontButtonLayout = CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT;
  SETTINGS.frontButtonBack = tempMapping[0];
  SETTINGS.frontButtonConfirm = tempMapping[1];
  SETTINGS.frontButtonLeft = tempMapping[2];
  SETTINGS.frontButtonRight = tempMapping[3];
  SETTINGS.enforceButtonLayoutConstraints();
}

bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = "Already assigned";
      return false;
    }
  }
  return true;
}

const char* ButtonRemapActivity::getRoleName(const uint8_t roleIndex) const {
  switch (roleIndex) {
    case 0:
      return "Back";
    case 1:
      return "Confirm";
    case 2:
      return "Left";
    case 3:
    default:
      return "Right";
  }
}

const char* ButtonRemapActivity::getHardwareName(const uint8_t buttonIndex) const {
  switch (buttonIndex) {
    case CrossPointSettings::FRONT_HW_BACK:
      return "Back (1st button)";
    case CrossPointSettings::FRONT_HW_CONFIRM:
      return "Confirm (2nd button)";
    case CrossPointSettings::FRONT_HW_LEFT:
      return "Left (3rd button)";
    case CrossPointSettings::FRONT_HW_RIGHT:
      return "Right (4th button)";
    default:
      return "Unknown";
  }
}

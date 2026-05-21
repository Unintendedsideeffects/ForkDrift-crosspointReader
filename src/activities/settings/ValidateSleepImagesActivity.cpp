#include "ValidateSleepImagesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "fontIds.h"

void ValidateSleepImagesActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  state = SCANNING;
  scanStarted = false;
  validCount = 0;
  invalidCount = 0;
  requestUpdate();
}

void ValidateSleepImagesActivity::onExit() { ActivityWithSubactivity::onExit(); }

void ValidateSleepImagesActivity::render(RenderLock&&) {
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawCenteredText(UI_12_FONT_ID, 15, tr(STR_VALIDATE_SLEEP_IMAGES), true, EpdFontFamily::BOLD);

  if (state == SCANNING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Scanning...", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  const char* headline = nullptr;
  char detailBuffer[48] = {};
  const char* detail = nullptr;

  if (validCount == 0 && invalidCount == 0) {
    headline = tr(STR_NO_SLEEP_IMAGES);
  } else if (invalidCount == 0 && validCount > 0) {
    headline = tr(STR_ALL_VALIDATED);
    snprintf(detailBuffer, sizeof(detailBuffer), "%d %s", validCount, validCount == 1 ? "image" : "images");
    detail = detailBuffer;
  } else {
    snprintf(detailBuffer, sizeof(detailBuffer), tr(STR_SLEEP_VALIDATION_RESULT), validCount, invalidCount);
    headline = detailBuffer;
  }

  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - (detail ? 15 : 0), headline, true, EpdFontFamily::BOLD);
  if (detail) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 15, detail, true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  renderer.drawButtonHints(UI_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ValidateSleepImagesActivity::loop() {
  if (state == SCANNING) {
    if (!scanStarted) {
      scanStarted = true;
      requestUpdateAndWait();
      LOG_INF("VALIDATE_SLEEP", "Starting sleep image validation");
      const SleepImageValidationStats stats = validateSleepImagesWithStats();
      validCount = stats.valid;
      invalidCount = stats.invalid;
      state = DONE;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
  }
}

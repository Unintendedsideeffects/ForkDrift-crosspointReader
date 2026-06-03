#include "ListPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ListPickerActivity::onEnter() {
  Activity::onEnter();
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(items_.size())) {
    selectedIndex_ = 0;
  }
  requestUpdate();
}

void ListPickerActivity::loop() {
  buttonNavigator_.onNext([this] {
    if (!items_.empty()) {
      selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(items_.size());
      requestUpdate();
    }
  });
  buttonNavigator_.onPrevious([this] {
    if (!items_.empty()) {
      selectedIndex_ = (selectedIndex_ + static_cast<int>(items_.size()) - 1) % static_cast<int>(items_.size());
      requestUpdate();
    }
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ListPickerResult r;
    r.selectedIndex = -1;
    ActivityResult result;
    result.data = r;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    ListPickerResult r;
    r.selectedIndex = selectedIndex_;
    ActivityResult result;
    result.data = r;
    setResult(std::move(result));
    finish();
  }
}

void ListPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, I18N.get(titleId_), nullptr);

  GUI.drawList(
      renderer,
      Rect{0, metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing, pageWidth,
           pageHeight -
               (metrics.topPadding + metrics.headerHeight + metrics.buttonHintsHeight + metrics.verticalSpacing * 2)},
      static_cast<int>(items_.size()), selectedIndex_, [this](int i) { return items_[i]; }, nullptr, nullptr, nullptr,
      false);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

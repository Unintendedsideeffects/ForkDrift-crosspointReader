#include "activities/util/ExtrasActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "activities/home/HomeExtras.h"
#include "components/UITheme.h"
#include "fontIds.h"

ExtrasActivity::ExtrasActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<HomeMenuId> items)
    : Activity("Extras", renderer, mappedInput), items(std::move(items)) {}

void ExtrasActivity::onEnter() {
  Activity::onEnter();
  // Resolve labels once: drawList asks for a row's title on every render, and
  // each tr() lookup would otherwise build a fresh std::string per visible row
  // per repaint.
  labels.clear();
  labels.reserve(items.size());
  std::transform(items.begin(), items.end(), std::back_inserter(labels),
                 [](HomeMenuId id) { return home_extras::label(id); });
  selectedIndex = 0;
  requestUpdate();
}

void ExtrasActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }

  if (!items.empty() && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    setResult(ActivityResult(ListPickerResult{selectedIndex}));
    finish();
    return;
  }

  buttonNavigator.onNext([this] {
    if (!items.empty()) {
      selectedIndex = (selectedIndex + 1) % static_cast<int>(items.size());
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (!items.empty()) {
      selectedIndex = (selectedIndex == 0) ? static_cast<int>(items.size()) - 1 : selectedIndex - 1;
      requestUpdate();
    }
  });
}

void ExtrasActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_EXTRAS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(items.size()), selectedIndex,
      [this](int index) { return labels[index]; }, nullptr,
      [this](int index) { return home_extras::icon(items[index]); });

  // Up/Down, not Previous/Next Page: this is a short vertical menu, and the
  // page labels are long enough to overlap each other in the hint row.
  const auto btnLabels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4);
  renderer.displayBuffer();
}

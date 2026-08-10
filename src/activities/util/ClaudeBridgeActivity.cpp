#include "ClaudeBridgeActivity.h"

#if ENABLE_CLAUDE_BRIDGE

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int kQuestionFontId = UI_12_FONT_ID;
constexpr int kMaxQuestionLines = 6;
}  // namespace

ClaudeBridgeActivity::ClaudeBridgeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           std::function<void()> onGoBack)
    : Activity("ClaudeBridge", renderer, mappedInput), onGoBack(std::move(onGoBack)) {}

void ClaudeBridgeActivity::onEnter() {
  Activity::onEnter();
  features::claude_bridge::attachActivity();
  requestUpdate();
}

void ClaudeBridgeActivity::onExit() {
  features::claude_bridge::detachActivity();
  Activity::onExit();
}

void ClaudeBridgeActivity::adoptPending(const features::claude_bridge::PendingRequest& next) {
  current = next;
  answers.clear();
  questionIndex = 0;
  selectedOption = 0;
}

void ClaudeBridgeActivity::clearCurrent() {
  current = features::claude_bridge::PendingRequest{};
  answers.clear();
  questionIndex = 0;
  selectedOption = 0;
}

void ClaudeBridgeActivity::cancelCurrent() {
  if (!current.id.empty()) {
    features::claude_bridge::cancelPending(current.id);
  }
  clearCurrent();
  requestUpdate();
}

void ClaudeBridgeActivity::selectCurrentOption() {
  if (current.id.empty() || questionIndex >= current.questions.size()) {
    return;
  }
  const auto& question = current.questions[questionIndex];
  if (selectedOption < 0 || selectedOption >= static_cast<int>(question.options.size())) {
    return;
  }

  answers.push_back({question.text, question.options[selectedOption].label});
  if (questionIndex + 1 < current.questions.size()) {
    questionIndex++;
    selectedOption = 0;
    requestUpdate();
    return;
  }

  features::claude_bridge::submitAnswers(current.id, answers);
  clearCurrent();
  requestUpdate();
}

void ClaudeBridgeActivity::loop() {
  if (current.id.empty()) {
    features::claude_bridge::PendingRequest next;
    if (features::claude_bridge::peekPending(next)) {
      adoptPending(next);
      requestUpdate();
      return;
    }
  } else if (!features::claude_bridge::isPending(current.id)) {
    clearCurrent();
    requestUpdate();
    return;
  }

  if (current.id.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) && onGoBack) {
      onGoBack();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelCurrent();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectCurrentOption();
    return;
  }

  const auto& options = current.questions[questionIndex].options;
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    selectedOption = (selectedOption + 1) % static_cast<int>(options.size());
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    selectedOption = (selectedOption + static_cast<int>(options.size()) - 1) % static_cast<int>(options.size());
    requestUpdate();
  }
}

void ClaudeBridgeActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (current.id.empty()) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_CLAUDE_HEADER),
                   nullptr);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
    const int lineHeight = renderer.getLineHeight(kQuestionFontId);
    const char* message =
        features::claude_bridge::isConfigured() ? tr(STR_CLAUDE_WAITING) : tr(STR_CLAUDE_SETUP_REQUIRED);
    renderer.drawCenteredText(kQuestionFontId, contentTop + (contentBottom - contentTop) / 2 - lineHeight, message);
    if (features::claude_bridge::isConfigured()) {
      renderer.drawCenteredText(SMALL_FONT_ID, contentTop + (contentBottom - contentTop) / 2 + lineHeight,
                                tr(STR_CLAUDE_ANSWERABLE));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto& question = current.questions[questionIndex];
  char progress[24];
  snprintf(progress, sizeof(progress), "%u/%u", static_cast<unsigned>(questionIndex + 1),
           static_cast<unsigned>(current.questions.size()));
  std::string subtitle = current.sessionTitle;
  if (!subtitle.empty()) {
    subtitle += " - ";
  }
  subtitle += progress;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, question.header.c_str(),
                 subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
  const int lineHeight = renderer.getLineHeight(kQuestionFontId);
  const auto questionLines =
      renderer.wrappedText(kQuestionFontId, question.text.c_str(), contentWidth, kMaxQuestionLines);

  int y = contentTop;
  for (const auto& line : questionLines) {
    renderer.drawText(kQuestionFontId, metrics.contentSidePadding, y, line.c_str());
    y += lineHeight;
  }
  y += metrics.verticalSpacing;

  const int listHeight = std::max(1, contentBottom - y);
  GUI.drawList(
      renderer, Rect{0, y, pageWidth, listHeight}, static_cast<int>(question.options.size()), selectedOption,
      [&question](const int index) { return question.options[index].label; },
      [&question](const int index) { return question.options[index].description; }, nullptr, nullptr, false);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif

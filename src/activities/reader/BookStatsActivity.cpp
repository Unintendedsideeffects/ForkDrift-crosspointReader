#include "BookStatsActivity.h"

#include "BookStatsView.h"
#include "MappedInputManager.h"
#include "ReadingProfileActivity.h"

BookStatsActivity::BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                     const BookReadingStats& stats, const GlobalReadingStats& globalStats)
    : Activity("BookStats", renderer, mappedInput), bookTitle(title), stats(stats), globalStats(globalStats) {}

void BookStatsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void BookStatsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startActivityForResult(std::make_unique<ReadingProfileActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
  }
}

void BookStatsActivity::render(RenderLock&&) {
  renderBookStatsView(renderer, &mappedInput, bookTitle, stats, globalStats, true);
  renderer.displayBuffer();
}

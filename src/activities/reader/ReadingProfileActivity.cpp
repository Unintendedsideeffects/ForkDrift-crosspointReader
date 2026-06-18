#include "ReadingProfileActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "ReadingStatsAnalytics.h"
#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint32_t DAILY_GOAL_MINUTES = 30;
constexpr uint64_t DAILY_GOAL_MS = static_cast<uint64_t>(DAILY_GOAL_MINUTES) * 60ULL * 1000ULL;
constexpr size_t RECENT_DAY_LIMIT = 6;

uint32_t latestReadingDayOrdinal(const ReadingStatsStore& store) {
  uint32_t latest = 0;
  for (const auto& day : store.getReadingDays()) {
    if (day.readingMs > 0) {
      latest = std::max(latest, day.dayOrdinal);
    }
  }
  return latest;
}

void drawMetricCell(GfxRenderer& renderer, int x, int w, int y, int h, const char* value, const char* label) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int gap = 6;
  const int textY = y + (h - valueLineH - gap - labelLineH) / 2;

  const int valueW = renderer.getTextWidth(UI_12_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, x + (w - valueW) / 2, textY, value, true, EpdFontFamily::BOLD);

  const int labelW = renderer.getTextWidth(SMALL_FONT_ID, label);
  renderer.drawText(SMALL_FONT_ID, x + (w - labelW) / 2, textY + valueLineH + gap, label, true);
}

std::string durationFromSeconds(uint32_t seconds) {
  return ReadingStatsAnalytics::formatDuration(static_cast<uint64_t>(seconds) * 1000ULL);
}

}  // namespace

ReadingProfileActivity::ReadingProfileActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ReadingProfile", renderer, mappedInput) {}

void ReadingProfileActivity::onEnter() {
  Activity::onEnter();
  ReadingStatsStore::getInstance().ensureLoaded();
  requestUpdate();
}

void ReadingProfileActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
  }
}

void ReadingProfileActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int contentX = metrics.contentSidePadding;
  const int contentW = screenW - metrics.contentSidePadding * 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_READING_STATS), nullptr);

  ReadingStatsStore& store = ReadingStatsStore::getInstance();
  const auto summary = ReadingStatsAnalytics::summarize(store);
  const uint32_t referenceDay = latestReadingDayOrdinal(store);
  const auto streaks = referenceDay > 0 ? ReadingStatsAnalytics::calculateStreaks(store, referenceDay, DAILY_GOAL_MS)
                                        : ReadingStatsAnalytics::StreakMetrics{};
  const auto goal = referenceDay > 0 ? ReadingStatsAnalytics::calculateGoalProgress(store, referenceDay, DAILY_GOAL_MINUTES)
                                     : ReadingStatsAnalytics::GoalProgress{};

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int cardH = 168;
  const int rowH = cardH / 2;
  const int thirdW = contentW / 3;

  renderer.drawRect(contentX, y, contentW, cardH);

  char value[32];
  std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(summary.totalPages));
  drawMetricCell(renderer, contentX, thirdW, y, rowH, value, tr(STR_STATS_PAGES_LBL));

  const std::string totalTime = durationFromSeconds(summary.totalSeconds);
  drawMetricCell(renderer, contentX + thirdW, thirdW, y, rowH, totalTime.c_str(), tr(STR_STATS_TIME_LBL));

  std::snprintf(value, sizeof(value), "%.0f min", summary.averageMinutesPerReadingDay);
  drawMetricCell(renderer, contentX + thirdW * 2, contentW - thirdW * 2, y, rowH, value, "Avg/day");

  y += rowH;
  renderer.drawLine(contentX, y, contentX + contentW, y);

  std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(streaks.currentStreak));
  drawMetricCell(renderer, contentX, thirdW, y, rowH, value, "Streak");

  std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(streaks.longestStreak));
  drawMetricCell(renderer, contentX + thirdW, thirdW, y, rowH, value, "Best");

  std::snprintf(value, sizeof(value), "%.0f%%", goal.percent > 999.0 ? 999.0 : goal.percent);
  drawMetricCell(renderer, contentX + thirdW * 2, contentW - thirdW * 2, y, rowH, value, "30m goal");

  y += rowH + metrics.verticalSpacing;

  const int footerReserve = metrics.buttonHintsHeight + metrics.verticalSpacing;
  const int listH = screenH - y - footerReserve;
  if (listH > 96) {
    renderer.drawRect(contentX, y, contentW, listH);
    renderer.drawText(UI_10_FONT_ID, contentX + 10, y + 10, "Recent days", true, EpdFontFamily::BOLD);

    const int lineH = renderer.getLineHeight(UI_10_FONT_ID) + 8;
    int lineY = y + 38;
    size_t shown = 0;
    const auto& days = store.getReadingDays();
    for (auto it = days.rbegin(); it != days.rend() && shown < RECENT_DAY_LIMIT; ++it) {
      if (it->readingMs == 0) {
        continue;
      }
      const std::string label = ReadingStatsAnalytics::formatDayLabel(it->dayOrdinal);
      const std::string duration = ReadingStatsAnalytics::formatDuration(it->readingMs);
      std::string line = label + "  " + duration;
      renderer.drawText(UI_10_FONT_ID, contentX + 12, lineY, line.c_str(), true);
      lineY += lineH;
      shown++;
    }
    if (shown == 0) {
      renderer.drawText(UI_10_FONT_ID, contentX + 12, lineY, "No reading stats yet", true);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#include "features/todo_planner/Registration.h"

#include <FeatureFlags.h>
#include <HalStorage.h>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/todo/DayDetailActivity.h"
#include "activities/todo/DayIndexActivity.h"
#include "activities/todo/TodoPlannerStorage.h"
#include "core/features/FeatureCatalog.h"
#include "core/registries/HomeActionRegistry.h"
#include "util/DateUtils.h"

namespace features::todo_planner {
namespace {

#if ENABLE_TODO_PLANNER
std::string resolveDailyPath(const std::string& date, const bool markdownEnabled) {
  const std::string markdownPath = "/daily/" + date + ".md";
  const std::string textPath = "/daily/" + date + ".txt";
  const bool markdownExists = Storage.exists(markdownPath.c_str());
  const bool textExists = Storage.exists(textPath.c_str());
  return TodoPlannerStorage::dailyPath(date, markdownEnabled, markdownExists, textExists);
}

void returnToDayIndex(void* ctx) {
  auto& manager = *static_cast<ActivityManager*>(ctx);
  manager.replaceActivity(
      std::make_unique<DayIndexActivity>(manager.getRenderer(), manager.getMappedInput(), &manager,
                                         [](void* backCtx) { static_cast<ActivityManager*>(backCtx)->goHome(); }));
}

static bool shouldExposeTodoPlannerHomeAction(core::HomeActionEntry::HomeActionContext ctx) {
  (void)ctx;
  return core::FeatureCatalog::isEnabled("todo_planner");
}

static Activity* createTodoPlannerHomeActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     void* callbackCtx, void (*onBack)(void* ctx)) {
  (void)callbackCtx;
  (void)onBack;

  const bool markdownEnabled = core::FeatureCatalog::isEnabled("markdown");
  const std::string today = DateUtils::currentDate();

  if (SETTINGS.todoOpenDirectToToday && !today.empty()) {
    const std::string filePath = resolveDailyPath(today, markdownEnabled);
    const std::string dateTitle = DateUtils::formatDayTitle(today);
    return new DayDetailActivity(renderer, mappedInput, filePath, today, dateTitle, &activityManager, returnToDayIndex);
  }

  return new DayIndexActivity(renderer, mappedInput, &activityManager,
                              [](void* ctx) { static_cast<ActivityManager*>(ctx)->goHome(); });
}
#endif

}  // namespace

void registerFeature() {
#if ENABLE_TODO_PLANNER
  core::HomeActionEntry homeEntry{};
  homeEntry.actionId = "todo_planner";
  homeEntry.shouldExpose = shouldExposeTodoPlannerHomeAction;
  homeEntry.create = createTodoPlannerHomeActionActivity;
  core::HomeActionRegistry::add(homeEntry);
#endif
}

}  // namespace features::todo_planner

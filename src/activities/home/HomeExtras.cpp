#include "activities/home/HomeExtras.h"

#include <FeatureFlags.h>
#include <I18n.h>

#include "components/themes/BaseTheme.h"
#include "core/features/FeatureModules.h"
#include "core/registries/HomeActionRegistry.h"

namespace home_extras {

std::vector<HomeMenuId> exposed() {
  std::vector<HomeMenuId> items;
  items.reserve(6);

  // Same predicates Home used when these were top-level tiles; moving them into
  // a submenu must not change *whether* a user can reach them.
  const bool todo = core::HomeActionRegistry::shouldExpose("todo_planner", {false});
  if (todo) items.push_back(HomeMenuId::Todo);
  if (core::HomeActionRegistry::shouldExpose("anki", {false})) items.push_back(HomeMenuId::Anki);
  // Notes is the fallback when no Planner exists; the two are mutually exclusive.
  if (core::FeatureModules::hasCapability(core::Capability::Notes) && !todo) items.push_back(HomeMenuId::Notes);
  // Both are registry-mediated: the feature owns the compile guard and, for
  // TRMNL, the paired-credentials precondition.
  if (core::HomeActionRegistry::shouldExpose("trmnl", {false})) items.push_back(HomeMenuId::Trmnl);
  if (core::HomeActionRegistry::shouldExpose("lua_plugins", {false})) items.push_back(HomeMenuId::Plugins);
  if (core::HomeActionRegistry::shouldExpose("claude_bridge", {false})) items.push_back(HomeMenuId::ClaudeBridge);

  return items;
}

bool isExtra(const HomeMenuId id) {
  switch (id) {
    case HomeMenuId::Todo:
    case HomeMenuId::Anki:
    case HomeMenuId::Notes:
    case HomeMenuId::Trmnl:
    case HomeMenuId::Plugins:
    case HomeMenuId::ClaudeBridge:
      return true;
    default:
      return false;
  }
}

std::string label(const HomeMenuId id, const bool gridStyle) {
  switch (id) {
    case HomeMenuId::Extras:
      return std::string(tr(STR_EXTRAS));
    case HomeMenuId::Todo:
      return gridStyle ? std::string("Agenda") : std::string(tr(STR_TODO_HOME_LABEL));
    case HomeMenuId::Anki:
      return "Anki";
    case HomeMenuId::Notes:
      return std::string(tr(STR_NOTES));
    case HomeMenuId::Trmnl:
      // Product name, deliberately untranslated.
      return "TRMNL";
    case HomeMenuId::Plugins:
      return "Plugins";
    case HomeMenuId::ClaudeBridge:
      return std::string(tr(STR_CLAUDE_TITLE));
    default:
      return "";
  }
}

UIIcon icon(const HomeMenuId id) {
  switch (id) {
    case HomeMenuId::Extras:
      // Folder despite Books above using it too. Icons resolve per theme AND per
      // size, and the tables are not the same set: ForkDriftTheme wires only
      // Folder/Settings/Transfer/Calendar at the 32px main-menu size, so
      // File/Text/Image/Library render as a blank gap on this tile (verified in
      // the simulator). Of what is actually available, Folder is the only one
      // that means "container of things". Do not "improve" this to a
      // better-sounding UIIcon without checking the target theme's 32px table.
      return UIIcon::Folder;
    case HomeMenuId::Todo:
      return UIIcon::Calendar;
    case HomeMenuId::Anki:
    case HomeMenuId::Notes:
      return UIIcon::Text;
    case HomeMenuId::Trmnl:
      return UIIcon::Image;
    case HomeMenuId::Plugins:
      return UIIcon::Folder;
    case HomeMenuId::ClaudeBridge:
      // NOT Settings: these render as 24px list rows now that they live inside
      // Extras, and Settings2Icon exists only at 32px — it drew as a blank gap
      // on device. File/Folder are in the 24px table; Text is taken by Anki.
      return UIIcon::File;
    default:
      return UIIcon::Settings;
  }
}

}  // namespace home_extras

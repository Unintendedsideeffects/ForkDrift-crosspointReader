#pragma once
#include <string>
#include <vector>

#include "activities/home/HomeActivity.h"

// The "Extras" bucket: app-like Home entries (Planner, Anki, Notes, Plugins,
// Claude, TRMNL) that are not part of the core reading flow. Home renders a
// single `HomeMenuId::Extras` tile; ExtrasActivity renders the bucket's
// contents. Both index the SAME ordered vector produced here, for the same
// reason `HomeActivity::menuModel` exists: a menu whose composition is derived
// twice eventually disagrees with itself and fires the wrong entry.
namespace home_extras {

// Ordered list of the extras currently exposed by capability/config. Empty when
// every extra is compiled out or unconfigured, in which case Home omits the
// Extras tile entirely rather than offering an empty submenu.
std::vector<HomeMenuId> exposed();

// True when `id` belongs to the Extras bucket (whether or not it is exposed).
bool isExtra(HomeMenuId id);

// Label/icon for an extras entry, including the Extras tile itself.
// HomeActivity::menuIdLabel/menuIdIcon delegate here so the tile on Home and
// the row inside Extras can never drift. `gridStyle` selects the shorter label
// the grid themes use, matching the existing Home convention.
std::string label(HomeMenuId id, bool gridStyle = false);
UIIcon icon(HomeMenuId id);

}  // namespace home_extras

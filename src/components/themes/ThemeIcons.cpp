#include "components/themes/ThemeIcons.h"

#include <Logging.h>

// UIIcon has 14 members (None through Bookmark); size the warned-set from a
// named constant so adding a new member does not silently leave the array too
// small — the constant is the only place to update.
static constexpr int kUIIconCount = 14;

// The warned-set is a static bool[icon][size-bucket], where size-bucket 0 is
// 24px (or any non-32 size) and size-bucket 1 is 32px. This keeps a miss on a
// repainting menu from flooding the serial log without any heap allocation.
namespace {
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
bool warned[kUIIconCount][2] = {};

void logOnce(UIIcon icon, int preferredSize, int fallbackSize) {
  const auto idx = static_cast<uint8_t>(icon);
  if (idx >= kUIIconCount) return;
  const int bucket = (preferredSize == 32) ? 1 : 0;
  if (warned[idx][bucket]) return;
  warned[idx][bucket] = true;
  LOG_WRN("ICON", "No bitmap for UIIcon %u at %dpx (or %dpx fallback); nothing drawn", static_cast<unsigned>(icon),
          preferredSize, fallbackSize);
}
}  // namespace

namespace theme_icons {

ResolvedIcon resolve(const uint8_t* (*lookup)(UIIcon, int), UIIcon icon, int preferredSize, int fallbackSize) {
  const uint8_t* bmp = lookup(icon, preferredSize);
  if (bmp != nullptr) return {bmp, preferredSize};

  if (fallbackSize != preferredSize && fallbackSize > 0) {
    bmp = lookup(icon, fallbackSize);
    if (bmp != nullptr) return {bmp, fallbackSize};
  }

  logOnce(icon, preferredSize, fallbackSize);
  return {nullptr, 0};
}

void warnMissingIcon(UIIcon icon, int size) {
  // Reuse logOnce: pass size as both preferred and fallback so the message
  // makes clear there is no alternative to fall back to.
  logOnce(icon, size, size);
}

}  // namespace theme_icons

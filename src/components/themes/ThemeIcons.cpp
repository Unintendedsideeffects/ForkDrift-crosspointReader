#include "components/themes/ThemeIcons.h"

#include <Logging.h>

#include "components/icons/book.h"
#include "components/icons/book24.h"
#include "components/icons/bookmark.h"
#include "components/icons/calendar.h"
#include "components/icons/calendar24.h"
#include "components/icons/file24.h"
#include "components/icons/folder.h"
#include "components/icons/folder24.h"
#include "components/icons/hotspot.h"
#include "components/icons/image24.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/text24.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"

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

const uint8_t* standard(const UIIcon icon, const int size) {
  if (size == 24) {
    switch (icon) {
      case UIIcon::Folder:
        return Folder24Icon;
      case UIIcon::Text:
        return Text24Icon;
      case UIIcon::Image:
        return Image24Icon;
      case UIIcon::Book:
        return Book24Icon;
      case UIIcon::File:
        return File24Icon;
      case UIIcon::Calendar:
        return Calendar24Icon;
      default:
        return nullptr;
    }
  }
  if (size == 32) {
    switch (icon) {
      case UIIcon::Folder:
        return FolderIcon;
      case UIIcon::Book:
        return BookIcon;
      case UIIcon::Recent:
        return RecentIcon;
      case UIIcon::Settings:
        return Settings2Icon;
      case UIIcon::Transfer:
        return TransferIcon;
      case UIIcon::Library:
        return LibraryIcon;
      case UIIcon::Wifi:
        return WifiIcon;
      case UIIcon::Hotspot:
        return HotspotIcon;
      case UIIcon::Calendar:
        return CalendarIcon;
      case UIIcon::Bookmark:
        return BookmarkIcon;
      default:
        return nullptr;
    }
  }
  return nullptr;
}

ResolvedIcon resolve(const uint8_t* (*lookup)(UIIcon, int), UIIcon icon, int preferredSize, int fallbackSize) {
  const uint8_t* bmp = lookup(icon, preferredSize);
  if (bmp != nullptr) return {bmp, preferredSize};

  if (fallbackSize != preferredSize && fallbackSize > 0) {
    bmp = lookup(icon, fallbackSize);
    if (bmp != nullptr) return {bmp, fallbackSize};
  }

  bmp = standard(icon, preferredSize);
  if (bmp != nullptr) return {bmp, preferredSize};
  if (fallbackSize != preferredSize && fallbackSize > 0) {
    bmp = standard(icon, fallbackSize);
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

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "AppCapabilities.h"
#if CROSSINK_SCALABLE_FONTS
#include <HalScalableFont.h>
#include <SdCardFontRegistry.h>
#endif

class GfxRenderer;

// App policy: an explicitly selected font only fills gaps in filename/book metadata.
class FilenameFontSystem {
 public:
  static constexpr const char* FONT_DIR = "/.crosspoint/languages/fonts";
  bool discover(std::vector<std::string>& names);
  bool ensureLoaded(GfxRenderer& renderer);
  void release(GfxRenderer& renderer);
  void invalidate() { dirty_.store(true, std::memory_order_release); }
  void invalidateForPath(const char* path);
  uint32_t fingerprint() const;

 private:
  std::atomic<bool> dirty_{true};
#if CROSSINK_SCALABLE_FONTS
  char attemptedFamily_[64] = {};
  std::unique_ptr<HalScalableFont> regular_, bold_;
  std::string regularPath_, boldPath_;
  bool scanFamily(const char* name, SdCardFontFamilyInfo& family);
#endif
};

extern FilenameFontSystem filenameFontSystem;

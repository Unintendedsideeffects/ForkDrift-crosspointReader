#include "UITheme.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <memory>

#include "MappedInputManager.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/ForkDriftTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "components/themes/pokemon/PokemonPartyTheme.h"
#include "components/themes/terminal/TerminalTheme.h"
#include "core/features/FeatureCatalog.h"
#include "features/status_overlay/Layout.h"
#include "util/RecentBooksStore.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

const ThemeMetrics& UITheme::getMetrics() const {
  adjustedMetrics = *currentMetrics;
  adjustedMetrics.topPadding += features::status_overlay::topInset();
  adjustedMetrics.homeTopPadding += features::status_overlay::topInset();
  adjustedMetrics.buttonHintsHeight += features::status_overlay::bottomInset();
  return adjustedMetrics;
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  if (!core::FeatureCatalog::isEnabled("lyra_theme")) {
    type = CrossPointSettings::UI_THEME::CLASSIC;
  } else if (type == CrossPointSettings::UI_THEME::MINIMAL && !core::FeatureCatalog::isEnabled("minimal_theme")) {
    type = CrossPointSettings::UI_THEME::LYRA;
  }
  switch (type) {
    case CrossPointSettings::UI_THEME::CLASSIC:
      LOG_DBG("UI", "Using Classic theme");
      currentTheme = std::make_unique<BaseTheme>();
      currentMetrics = &BaseMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA:
      LOG_DBG("UI", "Using Lyra theme");
      currentTheme = std::make_unique<LyraTheme>();
      currentMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_EXTENDED:
      LOG_DBG("UI", "Using Lyra Extended theme");
      currentTheme = std::make_unique<Lyra3CoversTheme>();
      currentMetrics = &Lyra3CoversMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::FORK_DRIFT:
      LOG_DBG("UI", "Using Fork Drift theme");
      currentTheme = std::make_unique<ForkDriftTheme>();
      currentMetrics = &ForkDriftMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::POKEMON_PARTY:
#if ENABLE_POKEMON_PARTY
      LOG_DBG("UI", "Using Pokemon Party theme");
      currentTheme = std::make_unique<PokemonPartyTheme>();
      currentMetrics = &PokemonPartyMetrics::values;
#else
      // Feature not compiled in: fall back to the grid theme it is based on.
      LOG_DBG("UI", "Pokemon Party unavailable; using Fork Drift theme");
      currentTheme = std::make_unique<ForkDriftTheme>();
      currentMetrics = &ForkDriftMetrics::values;
#endif
      break;
    case CrossPointSettings::UI_THEME::MINIMAL:
      LOG_DBG("UI", "Using Minimal theme");
      currentTheme = std::make_unique<MinimalTheme>();
      currentMetrics = &MinimalMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_CAROUSEL:
      LOG_DBG("UI", "Using Lyra Carousel theme");
      currentTheme = std::make_unique<LyraCarouselTheme>();
      currentMetrics = &LyraCarouselMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::TERMINAL:
      LOG_DBG("UI", "Using Terminal theme");
      currentTheme = std::make_unique<TerminalTheme>();
      currentMetrics = &TerminalMetrics::values;
      break;
  }
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle, int extraReservedHeight) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  auto orientation = renderer.getOrientation();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints && orientation != GfxRenderer::Orientation::LandscapeClockwise &&
      orientation != GfxRenderer::Orientation::LandscapeCounterClockwise) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight - extraReservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

// Screen area excluding the button hints
Rect UITheme::getScreenSafeArea(const GfxRenderer& renderer, bool hasFrontButtonHints, bool hasSideButtonHints) {
  auto orientation = renderer.getOrientation();
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  Rect safeArea = Rect{0, 0, screenWidth, screenHeight};
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      if (hasFrontButtonHints) {
        safeArea.height -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      if (hasFrontButtonHints) {
        safeArea.x += currentMetrics->buttonHintsHeight;
        safeArea.width -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      if (hasFrontButtonHints) {
        safeArea.y += currentMetrics->buttonHintsHeight;
        safeArea.height -= currentMetrics->buttonHintsHeight;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      if (hasFrontButtonHints) {
        safeArea.width -= currentMetrics->buttonHintsHeight;
      }
      break;
  }
  return safeArea;
}

namespace {
// Recent-book entries written before the [HEIGHT] template stored a concrete
// thumb file name ("thumb_120.bmp", "thumb_W56_H56.bmp"). Re-templating them
// here lets every consumer request any size for legacy entries; without this
// the substitution silently returns the old size and rescaled-dither bugs
// reappear for pre-template libraries.
std::string retemplateLegacyThumbPath(std::string coverBmpPath) {
  const size_t slash = coverBmpPath.find_last_of('/');
  const size_t base = (slash == std::string::npos) ? 0 : slash + 1;
  constexpr char kPrefix[] = "thumb_";
  constexpr char kSuffix[] = ".bmp";
  if (coverBmpPath.compare(base, sizeof(kPrefix) - 1, kPrefix) != 0) return coverBmpPath;
  if (coverBmpPath.size() < sizeof(kSuffix) ||
      coverBmpPath.compare(coverBmpPath.size() - (sizeof(kSuffix) - 1), sizeof(kSuffix) - 1, kSuffix) != 0) {
    return coverBmpPath;
  }
  coverBmpPath.replace(base + sizeof(kPrefix) - 1,
                       coverBmpPath.size() - (sizeof(kSuffix) - 1) - (base + sizeof(kPrefix) - 1), "[HEIGHT]");
  return coverBmpPath;
}
}  // namespace

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos == std::string::npos) {
    coverBmpPath = retemplateLegacyThumbPath(std::move(coverBmpPath));
    pos = coverBmpPath.find("[HEIGHT]", 0);
  }
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverWidth, int coverHeight) {
  // Replace [HEIGHT] with a WxH-specific suffix so carousel-sized thumbs get distinct file names.
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos == std::string::npos) {
    coverBmpPath = retemplateLegacyThumbPath(std::move(coverBmpPath));
    pos = coverBmpPath.find("[HEIGHT]", 0);
  }
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, "W" + std::to_string(coverWidth) + "_H" + std::to_string(coverHeight));
  }
  return coverBmpPath;
}

UIIcon UITheme::getFileIcon(const std::string& filename) {
  if (filename.back() == '/') {
    return Folder;
  }
  if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename)) {
    return Book;
  }
  if (FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename)) {
    return Text;
  }
  if (FsHelpers::hasBmpExtension(filename)) {
    return Image;
  }
  return File;
}

int UITheme::getStatusBarHeight() {
#if ENABLE_GLOBAL_STATUS_BAR
  return 0;
#else
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showStatusBar = SETTINGS.statusBarChapterPageCount || SETTINGS.statusBarBookProgressPercentage ||
                             SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE ||
                             SETTINGS.statusBarBattery;
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return (showStatusBar ? metrics.statusBarVerticalMargin : 0) +
         (showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0);
#endif
}

int UITheme::getProgressBarHeight() {
#if ENABLE_GLOBAL_STATUS_BAR
  return 0;
#else
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  const bool showProgressBar =
      SETTINGS.statusBarProgressBar != CrossPointSettings::STATUS_BAR_PROGRESS_BAR::HIDE_PROGRESS;
  return showProgressBar ? (((SETTINGS.statusBarProgressBarThickness + 1) * 2) + metrics.progressBarMarginTop) : 0;
#endif
}

// Centered text implementation that takes the safe area into account
void UITheme::drawCenteredText(const GfxRenderer& renderer, Rect screen, int fontId, int y, const char* text,
                               bool black, EpdFontFamily::Style style) {
  const int x = screen.x + (screen.width - renderer.getTextWidth(fontId, text, style)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

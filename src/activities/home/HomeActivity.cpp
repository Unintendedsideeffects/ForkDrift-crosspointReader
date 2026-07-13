#include "HomeActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <WiFi.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <optional>
#include <vector>

#if ENABLE_BOOKMARKS
#include "BookmarkStore.h"
#include "activities/home/BookmarksHomeActivity.h"
#endif
#if ENABLE_POKEMON_PARTY
#include "activities/home/PokemonAssignActivity.h"
#include "components/themes/pokemon/PokemonPartyTheme.h"
#include "util/PokemonPartySprites.h"
#endif
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "SpiBusMutex.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#if ENABLE_BOOKS_TAB_UI
#include "activities/books/BooksTabActivity.h"
#endif
#include "components/ScreenComponents.h"
#if ENABLE_LUA_PLUGINS
#include "activities/util/LuaActivity.h"
#include "activities/util/PluginListActivity.h"
#endif
#include "activities/home/HomeCarouselCache.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "core/features/FeatureModules.h"
#include "core/registries/HomeActionRegistry.h"
#include "features/status_overlay/Layout.h"
#include "fontIds.h"
#include "network/background/BackgroundWifiService.h"
#include "util/BookProgressDataStore.h"
#include "util/CoverThumbSizes.h"
#include "util/ForkDriftNavigation.h"
#include "util/LibraryShelfStore.h"
#include "util/RecentBooksStore.h"

namespace {
bool isOpdsShelfPath(const std::string& path) { return path.rfind("opds://", 0) == 0; }

const ThemeMetrics& homeMetrics() { return UITheme::getInstance().getMetrics(); }

bool homeUsesCarouselCache() { return homeMetrics().homeUsesCarouselCache; }

bool homeUsesDualSizeCoverThumbs() { return homeMetrics().homeUsesDualSizeCoverThumbs; }

bool homeIsCarouselNav() { return homeMetrics().homeNavigationMode == HomeNavigationMode::CarouselUnified; }

bool homeIsGridNav() { return homeMetrics().homeNavigationMode == HomeNavigationMode::CoverGridDualFocus; }

void getCarouselThumbSizes(int& centerW, int& centerH, int& sideW, int& sideH) {
  const ThemeMetrics& metrics = homeMetrics();
  centerW = metrics.homeCoverThumbCenterW;
  centerH = metrics.homeCoverThumbCenterH;
  sideW = metrics.homeCoverThumbSideW;
  sideH = metrics.homeCoverThumbSideH;
}

bool carouselCoverThumbsReady(const std::vector<RecentBook>& books) {
  if (books.empty()) {
    return false;
  }
  return std::all_of(books.begin(), books.end(), [](const RecentBook& book) {
    if (isOpdsShelfPath(book.path)) {
      return true;
    }
    if (book.coverBmpPath.empty()) {
      return false;
    }
    int centerW = 0;
    int centerH = 0;
    int sideW = 0;
    int sideH = 0;
    getCarouselThumbSizes(centerW, centerH, sideW, sideH);
    const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, centerW, centerH);
    const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, sideW, sideH);
    return !centerPath.empty() && !sidePath.empty() && Storage.exists(centerPath.c_str()) &&
           Storage.exists(sidePath.c_str());
  });
}

bool supportsGeneratedHomeCover(const RecentBook& book) {
  return FsHelpers::hasEpubExtension(book.path) || FsHelpers::hasXtcExtension(book.path);
}

bool hasCoverThumbTemplate(const std::string& coverBmpPath) {
  return coverBmpPath.find("[HEIGHT]") != std::string::npos;
}

bool isMissingAnyRegisteredCoverThumb(const std::string& coverBmpPath, const coverthumbs::Size* sizes,
                                      const int sizeCount) {
  if (!hasCoverThumbTemplate(coverBmpPath)) {
    return true;
  }
  for (int i = 0; i < sizeCount; ++i) {
    const coverthumbs::Size& size = sizes[i];
    const std::string thumbPath = size.width > 0 ? UITheme::getCoverThumbPath(coverBmpPath, size.width, size.height)
                                                 : UITheme::getCoverThumbPath(coverBmpPath, size.height);
    if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) {
      return true;
    }
  }
  return false;
}

int copyCoverThumbSizesForEpub(const coverthumbs::Size* source, const int sourceCount, Epub::ThumbSize* dest,
                               const int destMax) {
  if (source == nullptr || dest == nullptr || sourceCount <= 0 || destMax <= 0) {
    return 0;
  }
  const int count = std::min(sourceCount, destMax);
  for (int i = 0; i < count; ++i) {
    dest[i] = Epub::ThumbSize{source[i].width, source[i].height};
  }
  return count;
}

int copyCoverThumbSizesForXtc(const coverthumbs::Size* source, const int sourceCount, Xtc::ThumbSize* dest,
                              const int destMax) {
  if (source == nullptr || dest == nullptr || sourceCount <= 0 || destMax <= 0) {
    return 0;
  }
  const int count = std::min(sourceCount, destMax);
  for (int i = 0; i < count; ++i) {
    dest[i] = Xtc::ThumbSize{source[i].width, source[i].height};
  }
  return count;
}

void carouselCoverStateLookup(void*, const RecentBook& book, bool& centerExists, bool& sideExists) {
  int centerW = 0;
  int centerH = 0;
  int sideW = 0;
  int sideH = 0;
  getCarouselThumbSizes(centerW, centerH, sideW, sideH);
  const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, centerW, centerH);
  const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, sideW, sideH);
  centerExists = Storage.exists(centerPath.c_str());
  sideExists = Storage.exists(sidePath.c_str());
}

homecarousel::CacheGeometry carouselCacheGeometry(const GfxRenderer& renderer) {
  homecarousel::CacheGeometry geometry;
  geometry.frameBufferSize = static_cast<uint32_t>(renderer.getBufferSize());
  geometry.screenWidth = static_cast<uint16_t>(renderer.getScreenWidth());
  geometry.screenHeight = static_cast<uint16_t>(renderer.getScreenHeight());
  int centerW = 0;
  int centerH = 0;
  int sideW = 0;
  int sideH = 0;
  getCarouselThumbSizes(centerW, centerH, sideW, sideH);
  geometry.centerCoverW = static_cast<uint16_t>(centerW);
  geometry.centerCoverH = static_cast<uint16_t>(centerH);
  geometry.sideCoverW = static_cast<uint16_t>(sideW);
  geometry.sideCoverH = static_cast<uint16_t>(sideH);
  return geometry;
}

struct CarouselProgressContext {
  GfxRenderer* renderer = nullptr;
  Rect popupRect{};
};

void reportCarouselProgress(void* context, const int percent) {
  auto* progress = static_cast<CarouselProgressContext*>(context);
  progress->popupRect = GUI.drawPopup(*progress->renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(*progress->renderer, progress->popupRect, percent);
}

struct NegativeExistCache {
  static constexpr size_t CAPACITY = 64;
  size_t hashes[CAPACITY] = {0};
  bool occupied[CAPACITY] = {false};

  void clear() {
    for (size_t i = 0; i < CAPACITY; ++i) {
      occupied[i] = false;
    }
  }

  void add(size_t h) {
    size_t idx = h % CAPACITY;
    hashes[idx] = h;
    occupied[idx] = true;
  }

  bool contains(size_t h) const {
    size_t idx = h % CAPACITY;
    return occupied[idx] && hashes[idx] == h;
  }
};

static NegativeExistCache g_homeNegativeCache;

void maskCorners(const GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  const bool maskColor = (SETTINGS.darkMode != 0);
  for (int dy = 0; dy < r; dy++) {
    for (int dx = 0; dx < r; dx++) {
      if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r) {
        renderer.drawPixel(x + dx, y + dy, maskColor);                  // TL
        renderer.drawPixel(x + w - 1 - dx, y + dy, maskColor);          // TR
        renderer.drawPixel(x + dx, y + h - 1 - dy, maskColor);          // BL
        renderer.drawPixel(x + w - 1 - dx, y + h - 1 - dy, maskColor);  // BR
      }
    }
  }
}

}  // namespace
static_assert(HomeActivity::kMaxCachedBooks >= LyraCarouselMetrics::values.homeRecentBooksCount,
              "kMaxCachedBooks must cover all carousel slots");

// Static cover cache state
bool HomeActivity::coverRendered = false;
bool HomeActivity::coverBufferStored = false;
uint8_t* HomeActivity::coverBuffer = nullptr;
std::vector<std::string> HomeActivity::coverCacheBookPaths;
int HomeActivity::classicCoverCardWidth = 0;

std::string HomeActivity::fallbackTitleFromPath(const std::string& path) {
  auto title = path;
  const size_t lastSlash = title.find_last_of('/');
  if (lastSlash != std::string::npos) {
    title = title.substr(lastSlash + 1);
  }

  if (FsHelpers::checkFileExtension(title, ".xtch")) {
    title.resize(title.length() - 5);
  } else if (FsHelpers::checkFileExtension(title, ".epub") || FsHelpers::checkFileExtension(title, ".xtc") ||
             FsHelpers::checkFileExtension(title, ".txt") || FsHelpers::checkFileExtension(title, ".md")) {
    title.resize(title.length() - 4);
  }

  return title;
}

std::string HomeActivity::fallbackAuthor(const RecentBook& book) {
  if (!book.author.empty()) {
    return book.author;
  }
  return "";
}

bool HomeActivity::isPokemonPartyHomeMode() const {
  return SETTINGS.uiTheme == CrossPointSettings::POKEMON_PARTY &&
         core::FeatureModules::hasCapability(core::Capability::PokemonParty);
}

void HomeActivity::buildMenuModel() {
  menuModel.clear();
  menuModel.reserve(9);

#if ENABLE_BOOKMARKS
  hasBookmarks = core::FeatureModules::hasCapability(core::Capability::Bookmarks) && BookmarkStore::hasAnyBookmarks();
#endif
  const bool opds = core::HomeActionRegistry::shouldExpose("opds_browser", {hasOpdsServers});
  // Library = direct entry into the first configured OPDS catalog; same
  // feature exposure as the OPDS browser, gated on a server existing.
  const bool library = opds && !OPDS_STORE.getServers().empty();
  const bool todo = core::HomeActionRegistry::shouldExpose("todo_planner", {false});
  const bool anki = core::HomeActionRegistry::shouldExpose("anki", {false});
  const bool notes = core::FeatureModules::hasCapability(core::Capability::Notes) && !todo;

  // Grid (ForkDrift / Pokémon party): cover grid handles books; the button row
  // holds the actions. Composition matches what the grid actually renders.
  if (homeIsGridNav()) {
    menuModel.push_back(HomeMenuId::MyLibrary);
#if ENABLE_BOOKS_TAB_UI
    menuModel.push_back(HomeMenuId::BooksTab);
#endif
    if (library) menuModel.push_back(HomeMenuId::Library);
    if (todo) menuModel.push_back(HomeMenuId::Todo);
    if (anki) menuModel.push_back(HomeMenuId::Anki);
    if (notes) menuModel.push_back(HomeMenuId::Notes);
    menuModel.push_back(HomeMenuId::FileTransfer);
    menuModel.push_back(HomeMenuId::Settings);
#if ENABLE_LUA_PLUGINS
    menuModel.push_back(HomeMenuId::Plugins);
#endif
    return;
  }

  // Lyra carousel: book strip + icon menu row. First entry opens the centered
  // book; the rest are actions.
  if (homeIsCarouselNav()) {
    menuModel.push_back(HomeMenuId::OpenBook);
    menuModel.push_back(HomeMenuId::MyLibrary);
#if ENABLE_BOOKS_TAB_UI
    menuModel.push_back(HomeMenuId::BooksTab);
#endif
    if (library) menuModel.push_back(HomeMenuId::Library);
    if (opds) menuModel.push_back(HomeMenuId::Opds);
    if (todo) menuModel.push_back(HomeMenuId::Todo);
    if (anki) menuModel.push_back(HomeMenuId::Anki);
    if (notes) menuModel.push_back(HomeMenuId::Notes);
#if ENABLE_BOOKMARKS
    if (hasBookmarks) menuModel.push_back(HomeMenuId::Bookmarks);
#endif
    menuModel.push_back(HomeMenuId::FileTransfer);
    menuModel.push_back(HomeMenuId::Settings);
#if ENABLE_LUA_PLUGINS
    menuModel.push_back(HomeMenuId::Plugins);
#endif
    return;
  }

  // Classic list theme. Slot 0 is the "book card" (Continue Reading) when a book
  // is open; the remaining entries render as tiles below it.
  if (hasContinueReading) menuModel.push_back(HomeMenuId::ContinueReading);
  menuModel.push_back(HomeMenuId::MyLibrary);
#if ENABLE_BOOKS_TAB_UI
  menuModel.push_back(HomeMenuId::BooksTab);
#endif
  if (library) menuModel.push_back(HomeMenuId::Library);
  if (opds) menuModel.push_back(HomeMenuId::Opds);
  if (todo) menuModel.push_back(HomeMenuId::Todo);
  if (anki) menuModel.push_back(HomeMenuId::Anki);
  if (notes) menuModel.push_back(HomeMenuId::Notes);
  menuModel.push_back(HomeMenuId::FileTransfer);
  menuModel.push_back(HomeMenuId::Settings);
#if ENABLE_LUA_PLUGINS
  menuModel.push_back(HomeMenuId::Plugins);
#endif
}

void HomeActivity::loadRecentBooks() {
  g_homeNegativeCache.clear();
  auto metrics = UITheme::getInstance().getMetrics();
  const int maxBooks = metrics.homeRecentBooksCount;

  recentBooks.clear();
  const auto& storedBooks = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<size_t>(maxBooks), storedBooks.size()));

  for (const auto& stored : storedBooks) {
    if (recentBooks.size() >= static_cast<size_t>(maxBooks)) {
      break;
    }

    RecentBook entry = stored;
    if (entry.title.empty()) {
      entry.title = fallbackTitleFromPath(entry.path);
      if (entry.title != stored.title) {
        RECENT_BOOKS.updateBook(entry.path, entry.title, entry.author, entry.coverBmpPath);
      }
    }
    recentBooks.push_back(entry);
  }

  const auto shelfEntries = LIBRARY_SHELF.getSnapshot();
  recentBooks.reserve(recentBooks.size() + shelfEntries.size());
  for (const auto& shelfEntry : shelfEntries) {
    recentBooks.push_back({"opds://" + shelfEntry.href, shelfEntry.title, shelfEntry.author, ""});
  }

#if ENABLE_POKEMON_PARTY
  runPartySpritesSync();
#endif

  if (recentBooks.empty()) {
    selectedBookIndex = 0;
    return;
  }

  if (!APP_STATE.openEpubPath.empty()) {
    for (size_t i = 0; i < recentBooks.size(); ++i) {
      if (recentBooks[i].path == APP_STATE.openEpubPath) {
        selectedBookIndex = static_cast<int>(i);
        return;
      }
    }
  }

  selectedBookIndex = std::min(selectedBookIndex, static_cast<int>(recentBooks.size()) - 1);
  selectedBookIndex = std::max(0, selectedBookIndex);
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  SpiBusMutex::Guard guard;
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  const bool usesDualSizeCoverThumbs = homeUsesDualSizeCoverThumbs();
  const size_t recentBookCount = recentBooks.size();
  std::vector<char> bookUpdated(recentBookCount, false);
  const int progressIncrement = 90 / static_cast<int>(std::max<size_t>(1, recentBookCount));

  int progress = 0;
  for (size_t bookIdx = 0; bookIdx < recentBooks.size(); ++bookIdx) {
    RecentBook& book = recentBooks[bookIdx];
    if (isOpdsShelfPath(book.path)) {
      progress++;
      continue;
    }
    if (book.coverBmpPath.empty() && supportsGeneratedHomeCover(book)) {
      const auto recentBookData = core::FeatureModules::resolveRecentBookData(book.path);
      if (recentBookData.handled) {
        const std::string nextTitle = recentBookData.title.empty() ? book.title : recentBookData.title;
        const std::string nextAuthor = recentBookData.author.empty() ? book.author : recentBookData.author;
        if (nextTitle != book.title || nextAuthor != book.author || recentBookData.coverPath != book.coverBmpPath) {
          RECENT_BOOKS.updateBook(book.path, nextTitle, nextAuthor, recentBookData.coverPath);
          book.title = nextTitle;
          book.author = nextAuthor;
          book.coverBmpPath = recentBookData.coverPath;
        }
      }
    }

    if (!book.coverBmpPath.empty()) {
      coverthumbs::Size sizes[8] = {};
      const int sizeCount = coverthumbs::all(sizes, 8);
      const bool staleCoverPath = !hasCoverThumbTemplate(book.coverBmpPath);
      if (isMissingAnyRegisteredCoverThumb(book.coverBmpPath, sizes, sizeCount)) {
        if (FsHelpers::hasEpubExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
          if (!epub.load(false, true)) {
            LOG_ERR("HOME", "failed to load EPUB for thumb: %s", book.path.c_str());
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
            coverRendered = false;
            requestUpdate();
            progress++;
            continue;
          }
          Epub::ThumbSize epubSizes[8] = {};
          const int epubSizeCount = copyCoverThumbSizesForEpub(sizes, sizeCount, epubSizes, 8);
          const bool success = epub.generateThumbBmps(epubSizes, epubSizeCount);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          } else {
            if (staleCoverPath) {
              book.coverBmpPath = epub.getThumbBmpPath();
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
            }
            bookUpdated[bookIdx] = true;
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);
            Xtc::ThumbSize xtcSizes[8] = {};
            const int xtcSizeCount = copyCoverThumbSizesForXtc(sizes, sizeCount, xtcSizes, 8);
            const bool success = xtc.generateThumbBmps(xtcSizes, xtcSizeCount);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            } else {
              if (staleCoverPath) {
                book.coverBmpPath = xtc.getThumbBmpPath();
                RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
              }
              bookUpdated[bookIdx] = true;
            }
            coverRendered = false;
            requestUpdate();
          }
        } else if (!usesDualSizeCoverThumbs && !isPokemonPartyHomeMode()) {
          const auto homeCardData = core::FeatureModules::resolveHomeCardData(book.path, coverHeight);
          if (homeCardData.handled) {
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * progressIncrement);

            if (!homeCardData.coverPath.empty()) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, homeCardData.coverPath);
              book.coverBmpPath = homeCardData.coverPath;
            } else if (homeCardData.loaded) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;

  if (homeUsesCarouselCache()) {
    bool anyUpdated = false;
    for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
      if (static_cast<size_t>(i) >= bookUpdated.size() || !bookUpdated[i]) continue;
      anyUpdated = true;
      if (carouselFramesReady) {
        const int slot = homecarousel::HomeCarouselCache::shared().findFrameSlot(i);
        if (slot >= 0) renderCarouselFrame(i, slot);
      }
    }
    if (anyUpdated) {
      if (!carouselFramesReady) {
        if (Storage.exists(homecarousel::HomeCarouselCache::kCachePath))
          Storage.remove(homecarousel::HomeCarouselCache::kCachePath);
        if (Storage.exists(homecarousel::HomeCarouselCache::kCacheTmpPath))
          Storage.remove(homecarousel::HomeCarouselCache::kCacheTmpPath);
        preRenderCarouselFrames();
      } else {
        if (Storage.exists(homecarousel::HomeCarouselCache::kCachePath))
          Storage.remove(homecarousel::HomeCarouselCache::kCachePath);
        if (Storage.exists(homecarousel::HomeCarouselCache::kCacheTmpPath))
          Storage.remove(homecarousel::HomeCarouselCache::kCacheTmpPath);
      }
      requestUpdate();
    }
  }
}

void HomeActivity::openSelectedBook() {
  if (recentBooks.empty()) {
    return;
  }

  if (selectedBookIndex < 0 || selectedBookIndex >= static_cast<int>(recentBooks.size())) {
    selectedBookIndex = 0;
  }

  const auto& selected = recentBooks[static_cast<size_t>(selectedBookIndex)];
  if (isOpdsShelfPath(selected.path)) {
    onLibraryOpen();
    return;
  }
  if (!Storage.exists(selected.path.c_str())) {
    loadRecentBooks();
    requestUpdate();
    return;
  }

  freeCoverBuffer();
  homecarousel::HomeCarouselCache::shared().invalidate();
  carouselFramesReady = false;
  APP_STATE.openEpubPath = selected.path;
  APP_STATE.saveToFile();
  onContinueReading();
}

void HomeActivity::openCenteredBook() {
  selectedBookIndex = lastCarouselBookIndex;
  openSelectedBook();
}

std::string HomeActivity::menuIdLabel(const HomeMenuId id, const bool gridStyle) const {
  switch (id) {
    case HomeMenuId::ContinueReading:
      return "Continue Reading";
    case HomeMenuId::OpenBook:
      return recentBooks.empty() ? "Open Book (empty)" : "Open Book";
    case HomeMenuId::MyLibrary:
      return gridStyle ? std::string(tr(STR_BOOKS)) : std::string("My Library");
#if ENABLE_BOOKS_TAB_UI
    case HomeMenuId::BooksTab:
      return std::string(tr(STR_BOOKS_TAB));
#endif
    case HomeMenuId::Library:
      return std::string(tr(STR_LIBRARY));
    case HomeMenuId::Opds:
      return "OPDS Browser";
    case HomeMenuId::Todo:
      return gridStyle ? std::string("Agenda") : std::string(tr(STR_TODO_HOME_LABEL));
    case HomeMenuId::Anki:
      return "Anki";
    case HomeMenuId::Notes:
      return std::string(tr(STR_NOTES));
#if ENABLE_BOOKMARKS
    case HomeMenuId::Bookmarks:
      return tr(STR_BOOKMARKS);
#endif
    case HomeMenuId::FileTransfer:
      return std::string(tr(STR_FILE_TRANSFER));
#if ENABLE_POKEMON_PARTY
    case HomeMenuId::AssignPokemon:
      return std::string(tr(STR_PARTY_ASSIGN));
#endif
    case HomeMenuId::Settings:
      return std::string(tr(STR_SETTINGS_TITLE));
#if ENABLE_LUA_PLUGINS
    case HomeMenuId::Plugins:
      return "Plugins";
#endif
    default:
      return "";
  }
}

UIIcon HomeActivity::menuIdIcon(const HomeMenuId id) const {
  switch (id) {
    case HomeMenuId::ContinueReading:
    case HomeMenuId::OpenBook:
      return UIIcon::Book;
    case HomeMenuId::MyLibrary:
      return UIIcon::Folder;
#if ENABLE_BOOKS_TAB_UI
    case HomeMenuId::BooksTab:
      return UIIcon::Book;
#endif
    case HomeMenuId::Library:
      return UIIcon::Book;
    case HomeMenuId::Opds:
      return UIIcon::Library;
    case HomeMenuId::Todo:
      return UIIcon::Calendar;
    case HomeMenuId::Anki:
    case HomeMenuId::Notes:
      return UIIcon::Text;
#if ENABLE_BOOKMARKS
    case HomeMenuId::Bookmarks:
      return UIIcon::Book;
#endif
    case HomeMenuId::FileTransfer:
      return UIIcon::Transfer;
#if ENABLE_POKEMON_PARTY
    case HomeMenuId::AssignPokemon:
      return UIIcon::Book;
#endif
    case HomeMenuId::Settings:
#if ENABLE_LUA_PLUGINS
    case HomeMenuId::Plugins:
#endif
    default:
      return UIIcon::Settings;
  }
}

void HomeActivity::activateMenuId(const HomeMenuId id) {
  LOG_DBG("HOME", "activate id=%d selMenu=%d selBook=%d selector=%d hasCR=%d modelSize=%d", static_cast<int>(id),
          selectedMenuIndex, selectedBookIndex, selectorIndex, hasContinueReading ? 1 : 0,
          static_cast<int>(menuModel.size()));
  switch (id) {
    case HomeMenuId::ContinueReading:
      onContinueReading();
      break;
    case HomeMenuId::OpenBook:
      openCenteredBook();
      break;
    case HomeMenuId::MyLibrary:
      onMyLibraryOpen();
      break;
#if ENABLE_BOOKS_TAB_UI
    case HomeMenuId::BooksTab:
      onBooksTabOpen();
      break;
#endif
    case HomeMenuId::Library:
      onLibraryOpen();
      break;
    case HomeMenuId::Opds:
      onOpdsBrowserOpen();
      break;
    case HomeMenuId::Todo:
      onTodoOpen();
      break;
    case HomeMenuId::Anki:
      onAnkiOpen();
      break;
    case HomeMenuId::Notes:
      onNotesOpen();
      break;
#if ENABLE_BOOKMARKS
    case HomeMenuId::Bookmarks:
      onBookmarksOpen();
      break;
#endif
    case HomeMenuId::FileTransfer:
      onFileTransferOpen();
      break;
#if ENABLE_POKEMON_PARTY
    case HomeMenuId::AssignPokemon:
      onAssignPokemonOpen();
      break;
#endif
    case HomeMenuId::Settings:
      onSettingsOpen();
      break;
#if ENABLE_LUA_PLUGINS
    case HomeMenuId::Plugins:
      onPluginsOpen();
      break;
#endif
  }
}

bool HomeActivity::drawCoverAt(const std::string& coverPath, const int x, const int y, const int width,
                               const int height) const {
  if (coverPath.empty()) {
    return false;
  }

  size_t h = std::hash<std::string>{}(coverPath);
  if (g_homeNegativeCache.contains(h)) {
    return false;
  }

  if (!Storage.exists(coverPath.c_str())) {
    g_homeNegativeCache.add(h);
    return false;
  }

  SpiBusMutex::Guard guard;
  HalFile file;
  if (!Storage.openFileForRead("HOME", coverPath, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    renderer.drawBitmap(bitmap, x, y, width, height);
    maskCorners(renderer, x, y, width, height, 4);
  }
  file.close();
  return ok;
}

bool HomeActivity::blocksBackgroundServer() {
  if (recentsLoading || !firstRenderDone) {
    return true;
  }

  if (hasCoverImage && !coverRendered && !coverBufferStored) {
    return true;
  }

  if (homeUsesCarouselCache() && (carouselWarmupPending || (!carouselFramesReady && !recentBooks.empty()))) {
    return true;
  }

  return false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  const bool usesCarouselCache = homeUsesCarouselCache();
  firstRenderDone = false;
  carouselFramesReady = false;
  carouselWarmupPending = usesCarouselCache;
#if ENABLE_POKEMON_PARTY
  pokemonSpriteRefreshRetries = 0;
  pokemonSpriteCacheFingerprint = 0;
  retrySyncPending = false;
#endif

  if (blocksBackgroundServer() && BG_WIFI.isRunning()) {
    BG_WIFI.stop(true);
  }

  OPDS_STORE.loadFromFile();
  hasOpdsServers = OPDS_STORE.hasServers();
  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);
  const auto& metrics = UITheme::getInstance().getMetrics();

  if (mediaPickerEnabled) {
    loadRecentBooks();
    currentBookProgressPercent = -1.0f;
    if (!recentBooks.empty()) {
      BookProgressDataStore::ProgressData progress{};
      if (!isOpdsShelfPath(recentBooks[0].path) && BookProgressDataStore::loadProgress(recentBooks[0].path, progress)) {
        currentBookProgressPercent = progress.percent;
      }
    }

    // Reset selector; restore last carousel book position when re-entering
    selectorIndex = 0;
    lastCarouselBookIndex = 0;
    if (usesCarouselCache && !APP_STATE.openEpubPath.empty()) {
      for (int i = 0; i < static_cast<int>(recentBooks.size()); ++i) {
        if (recentBooks[i].path == APP_STATE.openEpubPath) {
          selectorIndex = i;
          lastCarouselBookIndex = i;
          break;
        }
      }
    }

    if (usesCarouselCache && !recentBooks.empty()) {
      loadBookProgress();
      std::string cacheKey;
      uint64_t cacheKeyHash = 0;
      homecarousel::HomeCarouselCache::buildCacheKey(recentBooks, cacheKey, cacheKeyHash, &carouselCoverStateLookup,
                                                     nullptr);
      const auto geometry = carouselCacheGeometry(renderer);
      auto& cache = homecarousel::HomeCarouselCache::shared();
      if (cacheKey == cache.key && (cache.frameCount > 0 || cache.keyHash != 0)) {
        carouselFramesReady = true;
        carouselWarmupPending = false;
      } else if (cache.hasValidDiskCache(cacheKeyHash, static_cast<int>(recentBooks.size()), geometry)) {
        preRenderCarouselFrames(false);
        if (carouselFramesReady) {
          carouselWarmupPending = false;
        }
      }
      if (carouselCoverThumbsReady(recentBooks) &&
          (cache.keyHash != 0 ||
           cache.hasValidDiskCache(cacheKeyHash, static_cast<int>(recentBooks.size()), geometry))) {
        recentsLoaded = true;
      }
    }

    selectedMenuIndex = 0;
    inButtonGrid = metrics.homeStartInMenuWhenEmpty && recentBooks.empty();
    hasContinueReading = !recentBooks.empty();

    hasCoverImage = false;
    coverBmpPath.clear();
    lastBookTitle.clear();
    lastBookAuthor.clear();

    // Invalidate cached cover buffer only when the recent book list has changed.
    // If the same books are shown, restoreCoverBuffer() will reuse the static
    // buffer and skip the slow SD card BMP reload entirely.
    // Also skip loadRecentCovers() — thumbnails were already verified last visit.
    if (isCoverCacheValid(metrics.homeCoverHeight, homeUsesDualSizeCoverThumbs())) {
      recentsLoaded = true;
    } else {
      freeCoverBuffer();
      coverRendered = false;
#if ENABLE_POKEMON_PARTY
      // Party cards are render-only. Their compact cache is produced by the
      // reader-idle worker, never during Home entry.
      if (isPokemonPartyHomeMode()) recentsLoaded = true;
#endif
    }

  } else {
    // Check if we have a book to continue reading
    hasContinueReading = !APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str());

    if (hasContinueReading) {
      // Extract filename from path for display
      lastBookTitle = APP_STATE.openEpubPath;
      const size_t lastSlash = lastBookTitle.find_last_of('/');
      if (lastSlash != std::string::npos) {
        lastBookTitle = lastBookTitle.substr(lastSlash + 1);
      }

      const int thumbHeight = renderer.getScreenHeight() / 2;
      const auto homeCardData = core::FeatureModules::resolveHomeCardData(APP_STATE.openEpubPath, thumbHeight);
      if (!homeCardData.title.empty()) {
        lastBookTitle = homeCardData.title;
      }
      if (!homeCardData.author.empty()) {
        lastBookAuthor = homeCardData.author;
      }
      if (!homeCardData.coverPath.empty()) {
        // coverPath is a [HEIGHT] template; this card draws the file directly,
        // so substitute the height we asked resolveHomeCardData to generate.
        coverBmpPath = UITheme::getCoverThumbPath(homeCardData.coverPath, thumbHeight);
        hasCoverImage = true;
      }

      // Preserve previous xtc fallback behavior when metadata is unavailable.
      if (homeCardData.handled && homeCardData.title.empty()) {
        if (FsHelpers::checkFileExtension(lastBookTitle, ".xtch")) {
          lastBookTitle.resize(lastBookTitle.length() - 5);
        } else if (FsHelpers::checkFileExtension(lastBookTitle, ".xtc")) {
          lastBookTitle.resize(lastBookTitle.length() - 4);
        }
      }
    }

    selectorIndex = 0;
  }

  // Build the single menu model now that hasContinueReading is known for both
  // the media-picker (grid/carousel) and classic-list paths.
  buildMenuModel();

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  freeCoverBuffer();
  homecarousel::HomeCarouselCache::shared().invalidate();
  carouselFramesReady = false;
  coverRendered = false;
  carouselWarmupPending = false;
  recentBooks.clear();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = renderer.getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  coverBufferStored = true;

  // Record which books' covers are now in the buffer so we can validate on re-entry.
  coverCacheBookPaths.resize(recentBooks.size());
  std::transform(recentBooks.begin(), recentBooks.end(), coverCacheBookPaths.begin(),
                 [](const RecentBook& book) { return book.path; });

  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = renderer.getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
  coverCacheBookPaths.clear();
}

bool HomeActivity::isCoverCacheValid(const int coverHeight, const bool usesDualSizeCoverThumbs) const {
  if (!coverBufferStored || !coverBuffer) {
    return false;
  }
  if (coverCacheBookPaths.size() != recentBooks.size()) {
    return false;
  }
  for (size_t i = 0; i < coverCacheBookPaths.size(); i++) {
    if (coverCacheBookPaths[i] != recentBooks[i].path) {
      return false;
    }
  }

  // cppcheck-suppress useStlAlgorithm -- multi-exit loop with mixed conditions; not expressible as std::all_of
  for (const auto& book : recentBooks) {
    if (isOpdsShelfPath(book.path)) {
      continue;
    }
    if (book.coverBmpPath.empty()) {
      if (supportsGeneratedHomeCover(book)) {
        return false;
      }
      continue;
    }
    if (!hasCoverThumbTemplate(book.coverBmpPath)) {
      return false;
    }

#if ENABLE_POKEMON_PARTY
    // Party Home consumes only its compact cache. A missing compact thumbnail
    // is an icon-rendering state, not a reason to synchronously bake on Home.
    if (isPokemonPartyHomeMode()) continue;
#endif

    if (usesDualSizeCoverThumbs) {
      int centerW = 0;
      int centerH = 0;
      int sideW = 0;
      int sideH = 0;
      getCarouselThumbSizes(centerW, centerH, sideW, sideH);
      const std::string centerPath = UITheme::getCoverThumbPath(book.coverBmpPath, centerW, centerH);
      const std::string sidePath = UITheme::getCoverThumbPath(book.coverBmpPath, sideW, sideH);
      if (centerPath.empty() || sidePath.empty() || !Storage.exists(centerPath.c_str()) ||
          !Storage.exists(sidePath.c_str())) {
        return false;
      }
      continue;
    }

    const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (thumbPath.empty() || !Storage.exists(thumbPath.c_str())) {
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Carousel frame cache
// ---------------------------------------------------------------------------

void HomeActivity::loadBookProgress() {
  const int count = std::min(static_cast<int>(recentBooks.size()), kMaxCachedBooks);
  for (int i = 0; i < count; ++i) {
    if (isOpdsShelfPath(recentBooks[i].path)) {
      cachedBookProgress[i] = -1.0f;
      continue;
    }
    BookProgressDataStore::ProgressData pd{};
    cachedBookProgress[i] = BookProgressDataStore::loadProgress(recentBooks[i].path, pd) ? pd.percent : -1.0f;
  }
  bookProgressCached = true;
}

void HomeActivity::renderCarouselFrameToCurrentBuffer(int bookIdx, float* outProgressPercent) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bookCount = static_cast<int>(recentBooks.size());
  bool dummy1 = false, dummy2 = false, dummy3 = false;
  const float frameProgressPercent = -1.0f;

  GUI.prepareCarouselFrame(bookIdx);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);
  GUI.drawRecentBookCover(
      renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight}, recentBooks, bookCount, dummy1,
      dummy2, dummy3, []() { return true; }, -1.0f);

  GUI.drawButtonMenu(
      renderer,
      Rect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing, pageWidth,
           pageHeight - (metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.verticalSpacing * 2 +
                         metrics.buttonHintsHeight)},
      static_cast<int>(menuModel.size()), -1, [this](int index) { return menuIdLabel(menuModel[index]); },
      [this](int index) { return menuIdIcon(menuModel[index]); });

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (outProgressPercent) *outProgressPercent = frameProgressPercent;
}

void HomeActivity::renderCarouselFrameCallback(void* context, const int bookIdx) {
  static_cast<HomeActivity*>(context)->renderCarouselFrameToCurrentBuffer(bookIdx, nullptr);
}

void HomeActivity::releaseCoverBufferCallback(void* context) { static_cast<HomeActivity*>(context)->freeCoverBuffer(); }

void HomeActivity::renderCarouselFrame(int bookIdx, int slotIdx) {
  const auto start = millis();
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  auto& cache = homecarousel::HomeCarouselCache::shared();
  if (!frameBuffer || slotIdx < 0 || slotIdx >= homecarousel::HomeCarouselCache::kFrameCount) return;

  // Free the destination cache slot while drawing. Carousel frames can be a full
  // framebuffer each; holding one during grayscale rendering can starve the BW
  // backup chunks and abort the render path on low-heap devices.
  cache.releaseSlot(slotIdx);

  renderCarouselFrameToCurrentBuffer(bookIdx, nullptr);
  freeCoverBuffer();

  if (cache.frameCount <= 0 || slotIdx < 0 || slotIdx >= cache.frameCount) {
    return;
  }

  const size_t bufferSize = renderer.getBufferSize();
  if (!cache.storeFrame(slotIdx, bookIdx, frameBuffer, bufferSize, ESP.getFreeHeap())) {
    LOG_DBG("HOME", "carousel: heap cache copy skipped for slot %d", slotIdx);
    return;
  }
  LOG_DBG("HOME", "carousel: renderCarouselFrame book=%d slot=%d took %lums", bookIdx, slotIdx, millis() - start);
}

void HomeActivity::updateSlidingWindowCache(int centerIdx, int bookCount) {
  (void)centerIdx;
  (void)bookCount;
  // One-frame cache: adjacent frames paged from SD snapshot on demand in render().
}

bool HomeActivity::preRenderCarouselFrames(bool showProgressPopup) {
  const int bookCount = static_cast<int>(recentBooks.size());
  if (bookCount == 0) return false;
  bool showedProgressPopup = false;
  auto& cache = homecarousel::HomeCarouselCache::shared();

  std::string newKey;
  uint64_t newKeyHash = 0;
  homecarousel::HomeCarouselCache::buildCacheKey(recentBooks, newKey, newKeyHash, &carouselCoverStateLookup, nullptr);

  if (newKey == cache.key && (cache.frameCount > 0 || cache.keyHash != 0)) {
    carouselFramesReady = true;
    return false;
  }

  if (!renderer.getFrameBuffer()) return false;
  freeCoverBuffer();
  cache.invalidate();

  const auto geometry = carouselCacheGeometry(renderer);
  const int targetFrameCount = std::min(bookCount, homecarousel::HomeCarouselCache::kFrameCount);
  const bool diskCacheValid = cache.hasValidDiskCache(newKeyHash, bookCount, geometry);

  if (!cache.allocateFrameSlots(targetFrameCount, geometry.frameBufferSize, ESP.getFreeHeap())) {
    LOG_DBG("HOME", "carousel: using disk-only cache (heap %u)", ESP.getFreeHeap());
  }

  const int selectedBookIdx = (selectorIndex < bookCount) ? selectorIndex : lastCarouselBookIndex;
  const int initialBookIdx = (selectedBookIdx >= 0 && selectedBookIdx < bookCount) ? selectedBookIdx : 0;

  auto loadOrRender = [&](int bookIdx, int slot) {
    if (diskCacheValid) {
      if (cache.frameCount > 0) {
        cache.loadFrameFromDisk(newKeyHash, bookCount, bookIdx, slot, geometry, ESP.getFreeHeap());
      }
      return;
    }
    if (cache.frameCount > 0) {
      renderCarouselFrame(bookIdx, slot);
      return;
    }
    renderCarouselFrameToCurrentBuffer(bookIdx, nullptr);
    freeCoverBuffer();
  };
  if (cache.frameCount > 0 || !diskCacheValid) {
    loadOrRender(initialBookIdx, 0);
  }
  cache.lastCenterIdx = initialBookIdx;

  cache.key = newKey;
  cache.keyHash = diskCacheValid ? newKeyHash : 0;
  carouselFramesReady = diskCacheValid || cache.frameCount > 0;
  coverRendered = false;
  coverBufferStored = false;

  if (!diskCacheValid && bookCount > 0) {
    CarouselProgressContext progressContext{&renderer, {}};
    const bool cacheBuilt = cache.buildCacheFile(
        newKeyHash, bookCount, geometry, renderer.getFrameBuffer(), &HomeActivity::renderCarouselFrameCallback, this,
        &HomeActivity::releaseCoverBufferCallback, this, showProgressPopup,
        showProgressPopup ? &reportCarouselProgress : nullptr, showProgressPopup ? &progressContext : nullptr);
    if (cacheBuilt) {
      cache.keyHash = newKeyHash;
      showedProgressPopup = true;
    }
  }
  return showedProgressPopup;
}

void HomeActivity::loop() {
#if ENABLE_POKEMON_PARTY
  if (retrySyncPending) {
    retrySyncPending = false;
    runPartySpritesSync();
    requestUpdate();
  }
#endif

  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);

  if (mediaPickerEnabled) {
    const bool leftPressed = mappedInput.wasPressed(MappedInputManager::Button::Left);
    const bool rightPressed = mappedInput.wasPressed(MappedInputManager::Button::Right);
    const bool upPressed = mappedInput.wasPressed(MappedInputManager::Button::Up);
    const bool downPressed = mappedInput.wasPressed(MappedInputManager::Button::Down);
    const bool carouselNav = homeIsCarouselNav();
    const bool pokemonPartyHomeMode = isPokemonPartyHomeMode();
    const int menuItemCount = static_cast<int>(menuModel.size());

    if (pokemonPartyHomeMode && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      activityManager.goToRecentBooks();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // All nav modes activate through the same menuModel — no per-mode index
      // schemes that can drift from what is rendered.
      if (carouselNav) {
        const int bookCount = static_cast<int>(recentBooks.size());
        if (bookCount == 0) {
          if (selectorIndex >= 0 && selectorIndex < static_cast<int>(menuModel.size())) {
            activateMenuId(menuModel[selectorIndex]);
          }
          return;
        }
        const bool inCarouselRow = (selectorIndex < bookCount);
        if (inCarouselRow) {
          selectedBookIndex = selectorIndex;
          openSelectedBook();
          return;
        }
        const int menuIdx = selectorIndex - bookCount;
        if (menuIdx >= 0 && menuIdx < static_cast<int>(menuModel.size())) {
          activateMenuId(menuModel[menuIdx]);
        }
        return;
      }

      if (!carouselNav) {
        // Grid and cover-menu themes share one focus model: the cover area XOR the
        // button row is active, and Confirm acts on whichever is focused. Cover-menu
        // previously always activated selectedMenuIndex while Left/Right moved a
        // *separate* cover cursor — so scrubbing the covers and pressing Confirm
        // kept firing menuModel[0] (Continue Reading -> open current book), and
        // Settings was unreachable. ForkDrift's grid avoided this by toggling focus;
        // cover-menu now does too. See home_menu_single_model: one model, no
        // per-mode cursor that can drift from what is rendered.
        const bool menuFocused = inButtonGrid || recentBooks.empty();
        if (menuFocused) {
          if (selectedMenuIndex >= 0 && selectedMenuIndex < static_cast<int>(menuModel.size())) {
            activateMenuId(menuModel[selectedMenuIndex]);
          }
        } else {
          openSelectedBook();
        }
        return;
      }
    }

    if (carouselNav) {
      const int bookCount = static_cast<int>(recentBooks.size());
      const int menuCount = static_cast<int>(menuModel.size());
      if (bookCount == 0) {
        if (leftPressed || downPressed) {
          selectorIndex = (selectorIndex + 1) % menuCount;
          requestUpdate();
        } else if (rightPressed || upPressed) {
          selectorIndex = (selectorIndex + menuCount - 1) % menuCount;
          requestUpdate();
        }
        return;
      }
      // Navigation, rendering, and activation all use menuModel — one source of
      // truth, so the menu-row count can never drift from what is activatable.
      const bool inCarouselRow = (selectorIndex < bookCount);

      if (leftPressed) {
        if (inCarouselRow) {
          selectorIndex = (selectorIndex + bookCount - 1) % bookCount;
          lastCarouselBookIndex = selectorIndex;
        } else {
          const int menuOffset = selectorIndex - bookCount;
          selectorIndex = bookCount + (menuOffset + menuCount - 1) % menuCount;
        }
        requestUpdate();
      } else if (rightPressed) {
        if (inCarouselRow) {
          selectorIndex = (selectorIndex + 1) % bookCount;
          lastCarouselBookIndex = selectorIndex;
        } else {
          const int menuOffset = selectorIndex - bookCount;
          selectorIndex = bookCount + (menuOffset + 1) % menuCount;
        }
        requestUpdate();
      } else if (downPressed) {
        if (inCarouselRow) {
          selectorIndex = bookCount;  // drop from the cover strip into the menu row
          requestUpdate();
        } else {
          // Already in the (horizontal) menu row: Down advances to the next item
          // too, not just Left/Right. Without this, Down past item 0 was a no-op,
          // trapping the user on the first action so Settings was unreachable by
          // the natural "press Down to go deeper" gesture.
          const int menuOffset = selectorIndex - bookCount;
          if (menuOffset < menuCount - 1) {
            selectorIndex = bookCount + menuOffset + 1;
            requestUpdate();
          }
        }
      } else if (upPressed) {
        if (!inCarouselRow) {
          const int menuOffset = selectorIndex - bookCount;
          if (menuOffset > 0) {
            selectorIndex = bookCount + menuOffset - 1;  // previous menu item
          } else {
            selectorIndex = lastCarouselBookIndex;  // step back up to the cover strip
          }
          requestUpdate();
        }
      }
      return;
    }

    // One nav path for every non-carousel theme (grid AND cover-menu). The only
    // difference between them is the cover layout — captured entirely by the
    // homeCoverGridColumns / homeCoverGridRows metrics — so navigateCoverGrid
    // handles a 1x1 single card, a 1xN cover row, and an MxN grid identically.
    // inButtonGrid is the single focus flag shared by nav, Confirm, and render;
    // bookCount == 0 forces menu focus so themes that don't start-in-menu when
    // empty are still navigable. (Carousel keeps its own linear-selector path.)
    if (!carouselNav) {
      const ThemeMetrics& navMetrics = homeMetrics();
      const int coverCols = navMetrics.homeCoverGridColumns;
      const int bookCount = static_cast<int>(recentBooks.size());
      const int coverRows = bookCount > navMetrics.homeCoverGridColumns ? navMetrics.homeCoverGridRows : 1;
      const bool menuFocus = inButtonGrid || bookCount == 0;

      if (menuFocus) {
        if (pokemonPartyHomeMode && navMetrics.homeMenuColumns > 1) {
          const int menuCols = navMetrics.homeMenuColumns;
          const auto nav = ForkDriftNavigation::navigateMenuGrid(selectedMenuIndex, menuItemCount, menuCols,
                                                                 leftPressed, rightPressed, upPressed, downPressed);
          if (nav.exitToCoverGrid && bookCount > 0) {
            inButtonGrid = false;
            selectedBookIndex = std::min(std::max(selectedBookIndex, 0), bookCount - 1);
            requestUpdate();
          } else if (nav.menuIndex != selectedMenuIndex) {
            selectedMenuIndex = nav.menuIndex;
            requestUpdate();
          }
        } else if (upPressed) {
          if (selectedMenuIndex == 0 && bookCount > 0) {
            inButtonGrid = false;
            selectedBookIndex = std::min(std::max(selectedBookIndex, 0), bookCount - 1);
            requestUpdate();
          } else if (selectedMenuIndex > 0) {
            selectedMenuIndex--;
            requestUpdate();
          }
        } else if (downPressed) {
          if (selectedMenuIndex < menuItemCount - 1) {
            selectedMenuIndex++;
            requestUpdate();
          }
        }
      } else if (leftPressed || rightPressed || upPressed || downPressed) {
        const auto nav = ForkDriftNavigation::navigateCoverGrid(selectedBookIndex, bookCount, coverCols, coverRows,
                                                                leftPressed, rightPressed, upPressed, downPressed);
        if (nav.enterButtonGrid) {
          inButtonGrid = true;
          selectedMenuIndex = 0;
        } else {
          selectedBookIndex = nav.bookIndex;
        }
        requestUpdate();
      }
    }
    return;
  }

  const int menuCount = static_cast<int>(menuModel.size());

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // selectorIndex indexes menuModel directly (slot 0 is the Continue Reading
    // book card when a book is open). One model → nav, render, and activation
    // stay aligned, so e.g. "Settings" can no longer trigger File Transfer.
    if (selectorIndex >= 0 && selectorIndex < static_cast<int>(menuModel.size())) {
      activateMenuId(menuModel[selectorIndex]);
    }
  }
}

void HomeActivity::render(RenderLock&&) {
  if (APP_STATE.consumeTransparentSleepWakePaint()) {
    firstRenderDone = true;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int topInset = features::status_overlay::topInset();
  const int usablePageHeight = pageHeight - topInset;

  // Carousel fast path: pre-rendered frames ready — memcpy + border overlay only
  if (carouselFramesReady && homeUsesCarouselCache()) {
    uint8_t* frameBuffer = renderer.getFrameBuffer();
    const int bookCount = static_cast<int>(recentBooks.size());
    const auto geometry = carouselCacheGeometry(renderer);
    auto& cache = homecarousel::HomeCarouselCache::shared();
    const bool inCarouselRow = (selectorIndex < bookCount);
    const int centerIdx = inCarouselRow ? selectorIndex : lastCarouselBookIndex;
    int slotIdx = cache.findFrameSlot(centerIdx);
    bool frameLoadedDirectToBuffer = false;

    if (frameBuffer && slotIdx < 0 && cache.keyHash != 0 && bookCount > 0) {
      if (cache.frameCount > 0) {
        const int evictSlot = cache.chooseEvictionSlot(centerIdx, bookCount);
        if (evictSlot >= 0 &&
            cache.loadFrameFromDisk(cache.keyHash, bookCount, centerIdx, evictSlot, geometry, ESP.getFreeHeap())) {
          slotIdx = evictSlot;
        } else if (cache.readFrameFromDisk(cache.keyHash, bookCount, centerIdx, geometry, frameBuffer)) {
          slotIdx = 0;
          frameLoadedDirectToBuffer = true;
        }
      } else if (cache.readFrameFromDisk(cache.keyHash, bookCount, centerIdx, geometry, frameBuffer)) {
        slotIdx = 0;
        frameLoadedDirectToBuffer = true;
      }
    }

    if (frameBuffer && slotIdx >= 0 && (cache.frames[slotIdx] || frameLoadedDirectToBuffer || cache.frameCount <= 0)) {
      if (cache.frames[slotIdx]) {
        memcpy(frameBuffer, cache.frames[slotIdx], renderer.getBufferSize());
      }
      GUI.prepareCarouselFrame(centerIdx);

      GUI.drawCarouselBorder(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                             recentBooks, centerIdx, inCarouselRow);
      if (!inCarouselRow) {
        const int menuOverlayIdx = selectorIndex - bookCount;
        GUI.drawCarouselMenuSelectionOverlay(
            renderer, static_cast<int>(menuModel.size()), menuOverlayIdx,
            [this](int index) { return menuIdLabel(menuModel[index]); },
            [this](int index) { return menuIdIcon(menuModel[index]); });
      }

      float frameProgressPercent = -1.0f;
      if (centerIdx >= 0 && centerIdx < bookCount) {
        if (bookProgressCached && centerIdx < kMaxCachedBooks) {
          frameProgressPercent = cachedBookProgress[centerIdx];
        } else {
          BookProgressDataStore::ProgressData pd{};
          if (!isOpdsShelfPath(recentBooks[centerIdx].path) &&
              BookProgressDataStore::loadProgress(recentBooks[centerIdx].path, pd)) {
            frameProgressPercent = pd.percent;
          }
        }
      }
      if (frameProgressPercent >= 0.0f) {
        GUI.drawCarouselProgressOverlay(renderer,
                                        Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                                        recentBooks, centerIdx, frameProgressPercent);
      }

      // Full refresh only when pendingHomeFullRefresh (reader exit / explicit boot);
      // Home ↔ Settings and carousel slides stay on FAST.
      const bool doFullCarousel =
          !firstRenderDone && APP_STATE.pendingHomeFullRefresh && !APP_STATE.transparentSleepRestoredOnWake;
      if (doFullCarousel) APP_STATE.pendingHomeFullRefresh = false;
      renderer.displayBuffer(doFullCarousel ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
      updateSlidingWindowCache(centerIdx, bookCount);
      if (!firstRenderDone) {
        firstRenderDone = true;
        requestUpdate();
      } else if (!recentsLoaded && !recentsLoading) {
        const bool diskCarouselReady = cache.keyHash != 0 && carouselCoverThumbsReady(recentBooks);
        if (diskCarouselReady) {
          recentsLoaded = true;
        } else {
          recentsLoading = true;
          loadRecentCovers(metrics.homeCoverHeight);
        }
      }
      return;
    }
  }

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();
  // If we are using the new media picker UI, use its specialized rendering
  const bool mediaPickerEnabled = core::FeatureModules::hasCapability(core::Capability::HomeMediaPicker);
  if (mediaPickerEnabled) {
    const bool gridNav = homeIsGridNav();
    const bool carouselNav = homeIsCarouselNav();
    // Grid and CoverMenu split into cover + menu with inButtonGrid focus.
    // Carousel uses unified selectorIndex for both regions in nav and render.
    const bool regionFocusNav = !carouselNav;
    // Same focus predicate the nav and Confirm paths use, so the visible cursor
    // always sits on the region Confirm will act on (no books => menu focused).
    const bool menuFocused = inButtonGrid || recentBooks.empty();
    const int bookCountRender = static_cast<int>(recentBooks.size());
    const int coverSelector = carouselNav ? selectorIndex : (regionFocusNav && menuFocused ? -1 : selectedBookIndex);
    const int menuSelector = carouselNav ? (selectorIndex >= bookCountRender ? selectorIndex - bookCountRender : -1)
                                         : (regionFocusNav && !menuFocused ? -1 : selectedMenuIndex);
    const int singleRowH = metrics.homeCoverTileHeight / metrics.homeCoverGridRows;
    const int coverTileH_raw =
        gridNav ? ((bookCountRender > metrics.homeCoverGridColumns ? metrics.homeCoverGridRows : 1) * singleRowH)
                : metrics.homeCoverTileHeight;
    const int menuCols = std::max(1, metrics.homeMenuColumns);
    const int menuRowsForLayout = (static_cast<int>(menuModel.size()) + menuCols - 1) / menuCols;
    const int menuMinH = metrics.verticalSpacing * 2 + metrics.buttonHintsHeight +
                         menuRowsForLayout * metrics.menuRowHeight +
                         std::max(0, menuRowsForLayout - 1) * metrics.menuSpacing;
    const int coverTileH = gridNav ? std::min(coverTileH_raw, usablePageHeight - menuMinH) : coverTileH_raw;

    GUI.drawRecentBookCover(
        renderer, Rect(0, topInset, pageWidth, coverTileH), recentBooks, coverSelector, coverRendered,
        coverBufferStored, bufferRestored, [this]() { return storeCoverBuffer(); }, currentBookProgressPercent);

    std::vector<std::string> menuLabels;
    std::vector<UIIcon> menuIcons;
    menuLabels.reserve(6);
    menuIcons.reserve(6);

    // Grid uses its own label flavour ("Books"/"Agenda"); both flavours render
    // the same menuModel, so what is shown always matches what activates.
    menuLabels.reserve(menuModel.size());
    menuIcons.reserve(menuModel.size());
    for (const HomeMenuId id : menuModel) {
      menuLabels.push_back(menuIdLabel(id, gridNav));
      menuIcons.push_back(menuIdIcon(id));
    }

    GUI.drawButtonMenu(
        renderer,
        Rect{0, topInset + coverTileH + metrics.verticalSpacing, pageWidth,
             usablePageHeight - (coverTileH + metrics.verticalSpacing * 2 + metrics.buttonHintsHeight)},
        static_cast<int>(menuLabels.size()), menuSelector, [&menuLabels](const int index) { return menuLabels[index]; },
        [&menuIcons](const int index) { return menuIcons[index]; });

    const char* backLabel = isPokemonPartyHomeMode() ? tr(STR_RECENTS) : "";
    const bool pokemonMenuFocus = isPokemonPartyHomeMode() && (inButtonGrid || recentBooks.empty());
    const char* dirHintA = pokemonMenuFocus ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP);
    const char* dirHintB = pokemonMenuFocus ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN);
    const auto labels = mappedInput.mapLabels(backLabel, tr(STR_SELECT), dirHintA, dirHintB);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    constexpr int margin = 20;

    // --- Top "book" card for the current title (selectorIndex == 0) ---
    const int bookWidth = pageWidth / 2;
    const int bookHeight = pageHeight / 2;
    const int bookX = (pageWidth - bookWidth) / 2;
    const int bookY = topInset + 30;
    const bool bookSelected = hasContinueReading && selectorIndex == 0;

    // Bookmark dimensions (used in multiple places)
    const int bookmarkWidth = bookWidth / 8;
    const int bookmarkHeight = bookHeight / 5;
    const int bookmarkX = bookX + bookWidth - bookmarkWidth - 10;
    const int bookmarkY = bookY + 5;

    // Draw book card regardless, fill with message based on `hasContinueReading`
    {
      // Draw cover image as background if available (inside the box)
      // Only load from SD on first render, then use stored buffer
      if (hasContinueReading && hasCoverImage && !coverBmpPath.empty() && !coverRendered) {
        // First time: load cover from SD and render
        SpiBusMutex::Guard guard;
        HalFile file;
        if (Storage.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // Size the card to the cover's aspect ratio (full card height, width
            // capped to the fixed box) and centre it, matching how the grid and
            // carousel themes render covers via BaseTheme::computeBookCardRect.
            // drawBitmap preserves aspect ratio and fits within the rect, so
            // filling an aspect-sized rect renders the cover edge-to-edge with a
            // tight border — the same result a portrait cover gets elsewhere,
            // instead of being letterboxed inside a fixed wide box.
            int coverW = bookWidth;
            const int coverH = bookHeight;
            const int imgW = bitmap.getWidth();
            const int imgH = bitmap.getHeight();
            if (imgW > 0 && imgH > 0) {
              coverW = static_cast<int>(coverH * (static_cast<float>(imgW) / static_cast<float>(imgH)));
              if (coverW > bookWidth) coverW = bookWidth;
            }
            const int coverX = (pageWidth - coverW) / 2;
            const int coverY = bookY;
            classicCoverCardWidth = coverW;

            renderer.drawBitmap(bitmap, coverX, coverY, coverW, coverH);
            renderer.drawRect(coverX, coverY, coverW, coverH);

            // Store the buffer with cover image for fast navigation
            coverBufferStored = storeCoverBuffer();
            coverRendered = true;

            // First render: if selected, draw selection indicators now
            if (bookSelected) {
              renderer.drawRect(coverX + 1, coverY + 1, coverW - 2, coverH - 2);
              renderer.drawRect(coverX + 2, coverY + 2, coverW - 4, coverH - 4);
            }
          }
          file.close();
        }
      } else if (!bufferRestored && !coverRendered) {
        // No cover image: draw border or fill, plus bookmark as visual flair
        if (bookSelected) {
          renderer.fillRect(bookX, bookY, bookWidth, bookHeight);
        } else {
          renderer.drawRect(bookX, bookY, bookWidth, bookHeight);
        }

        // Draw bookmark ribbon when no cover image (visual decoration)
        if (hasContinueReading) {
          const int notchDepth = bookmarkHeight / 3;
          const int centerX = bookmarkX + bookmarkWidth / 2;

          const int xPoints[5] = {
              bookmarkX,                  // top-left
              bookmarkX + bookmarkWidth,  // top-right
              bookmarkX + bookmarkWidth,  // bottom-right
              centerX,                    // center notch point
              bookmarkX                   // bottom-left
          };
          const int yPoints[5] = {
              bookmarkY,                                // top-left
              bookmarkY,                                // top-right
              bookmarkY + bookmarkHeight,               // bottom-right
              bookmarkY + bookmarkHeight - notchDepth,  // center notch point
              bookmarkY + bookmarkHeight                // bottom-left
          };

          // Draw bookmark ribbon (inverted if selected)
          renderer.fillPolygon(xPoints, yPoints, 5, !bookSelected);
        }
      }

      // If buffer was restored, draw selection indicators if needed. Hug the
      // aspect-sized cover rect (matching first render) rather than the fixed
      // box, so the selection border lines up with the stored cover + border.
      if (bufferRestored && bookSelected && coverRendered) {
        const int selW = classicCoverCardWidth > 0 ? classicCoverCardWidth : bookWidth;
        const int selX = (pageWidth - selW) / 2;
        renderer.drawRect(selX + 1, bookY + 1, selW - 2, bookHeight - 2);
        renderer.drawRect(selX + 2, bookY + 2, selW - 4, bookHeight - 4);
      }
    }

    if (hasContinueReading) {
      // Split into words
      std::vector<std::string> words;
      size_t pos = 0;
      while (pos < lastBookTitle.size()) {
        while (pos < lastBookTitle.size() && lastBookTitle[pos] == ' ') ++pos;
        if (pos >= lastBookTitle.size()) break;
        size_t start = pos;
        while (pos < lastBookTitle.size() && lastBookTitle[pos] != ' ') ++pos;
        words.push_back(lastBookTitle.substr(start, pos - start));
      }

      std::vector<std::string> lines;
      std::string currentLine;
      const int maxLineWidth = bookWidth - 40;
      const int spaceWidth = renderer.getSpaceWidth(UI_12_FONT_ID);

      for (auto& word : words) {
        if (lines.size() >= 3) {
          lines.back() += "...";
          break;
        }
        int wordWidth = renderer.getTextWidth(UI_12_FONT_ID, word.c_str());
        if (wordWidth > maxLineWidth) {
          // 120 char boundaries (240B stack, under the <256B local rule): a
          // word longer than that can never fit on a line, so the binary
          // search outcome is unchanged by the cap.
          uint16_t utf8Indices[120];
          int utf8Count = 0;
          for (size_t i = 0; i < word.size() && i <= UINT16_MAX; ++i) {
            if ((static_cast<unsigned char>(word[i]) & 0xC0) != 0x80) {
              if (utf8Count < 120) {
                utf8Indices[utf8Count++] = static_cast<uint16_t>(i);
              } else {
                break;
              }
            }
          }

          int low = 0;
          int high = utf8Count;
          int bestMid = 0;

          while (low <= high) {
            int mid = low + (high - low) / 2;
            size_t len = (mid < utf8Count) ? utf8Indices[mid] : word.size();
            std::string candidate = word.substr(0, len) + "...";
            if (renderer.getTextWidth(UI_12_FONT_ID, candidate.c_str()) <= maxLineWidth) {
              bestMid = mid;
              low = mid + 1;
            } else {
              high = mid - 1;
            }
          }

          size_t finalLen = (bestMid < utf8Count) ? utf8Indices[bestMid] : word.size();
          word = word.substr(0, finalLen) + "...";
        }

        int curWidth = renderer.getTextWidth(UI_12_FONT_ID, currentLine.c_str());
        if (!currentLine.empty() && curWidth + spaceWidth + wordWidth > maxLineWidth) {
          lines.push_back(currentLine);
          currentLine = word;
        } else {
          if (!currentLine.empty()) currentLine += " ";
          currentLine += word;
        }
      }
      if (!currentLine.empty() && lines.size() < 3) lines.push_back(currentLine);

      int totalTextHeight = renderer.getLineHeight(UI_12_FONT_ID) * lines.size();
      if (!lastBookAuthor.empty()) totalTextHeight += renderer.getLineHeight(UI_10_FONT_ID) * 1.5;

      int titleYStart = bookY + (bookHeight - totalTextHeight) / 2;

      if (coverRendered) {
        // Draw background box for text legibility over cover
        const int maxW =
            std::accumulate(lines.begin(), lines.end(), 0, [this](const int maxWidth, const std::string& line) {
              return std::max(maxWidth, renderer.getTextWidth(UI_12_FONT_ID, line.c_str()));
            });
        int boxW = maxW + 16;
        int boxH = totalTextHeight + 16;
        renderer.fillRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, bookSelected);
        renderer.drawRect((pageWidth - boxW) / 2, titleYStart - 8, boxW, boxH, !bookSelected);
      }

      for (const auto& l : lines) {
        renderer.drawCenteredText(UI_12_FONT_ID, titleYStart, l.c_str(), !bookSelected);
        titleYStart += renderer.getLineHeight(UI_12_FONT_ID);
      }

      if (!lastBookAuthor.empty()) {
        titleYStart += renderer.getLineHeight(UI_10_FONT_ID) * 0.5;
        std::string author = lastBookAuthor;
        if (renderer.getTextWidth(UI_10_FONT_ID, author.c_str()) > maxLineWidth) {
          int utf8Indices[256];
          int utf8Count = 0;
          for (size_t i = 0; i < author.size(); ++i) {
            if ((static_cast<unsigned char>(author[i]) & 0xC0) != 0x80) {
              if (utf8Count < 256) {
                utf8Indices[utf8Count++] = i;
              } else {
                break;
              }
            }
          }

          int low = 0;
          int high = utf8Count;
          int bestMid = 0;

          while (low <= high) {
            int mid = low + (high - low) / 2;
            size_t len = (mid < utf8Count) ? utf8Indices[mid] : author.size();
            std::string candidate = author.substr(0, len) + "...";
            if (renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) <= maxLineWidth) {
              bestMid = mid;
              low = mid + 1;
            } else {
              high = mid - 1;
            }
          }

          size_t finalLen = (bestMid < utf8Count) ? utf8Indices[bestMid] : author.size();
          author = author.substr(0, finalLen) + "...";
        }
        renderer.drawCenteredText(UI_10_FONT_ID, titleYStart, author.c_str(), !bookSelected);
      }

      const int continueY = bookY + bookHeight - renderer.getLineHeight(UI_10_FONT_ID) * 1.5;
      renderer.drawCenteredText(UI_10_FONT_ID, continueY, "Continue Reading", !bookSelected);
    } else {
      int y = bookY + (bookHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
      renderer.drawCenteredText(UI_12_FONT_ID, y, "No open book");
    }

    // Draw other menu items
    int menuStartY = bookY + bookHeight + 30;
    int menuTileWidth = pageWidth - 40;
    int menuTileHeight = 45;
    int menuSpacing = 10;

    // Tiles below the book card are the menuModel entries other than the
    // Continue Reading slot (which is rendered as the card above). Same model
    // drives selection, so the visible tile and the activated action always
    // agree (previously "Settings" could trigger File Transfer).
    std::vector<std::string> labels_text;
    labels_text.reserve(menuModel.size());
    for (const HomeMenuId id : menuModel) {
      if (id == HomeMenuId::ContinueReading) continue;
      labels_text.push_back(menuIdLabel(id));
    }
    for (size_t i = 0; i < labels_text.size(); ++i) {
      int tileY = menuStartY + i * (menuTileHeight + menuSpacing);
      bool selected = (selectorIndex == (int)i + (hasContinueReading ? 1 : 0));
      if (selected)
        renderer.fillRect(20, tileY, menuTileWidth, menuTileHeight);
      else
        renderer.drawRect(20, tileY, menuTileWidth, menuTileHeight);
      renderer.drawCenteredText(UI_10_FONT_ID, tileY + (menuTileHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                                labels_text[i].c_str(), !selected);
    }

    const auto hints = mappedInput.mapLabels("", "Select", "Up", "Down");
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  }

  bool doFull = !firstRenderDone && APP_STATE.pendingHomeFullRefresh && !APP_STATE.transparentSleepRestoredOnWake;
  if (doFull) APP_STATE.pendingHomeFullRefresh = false;
#if ENABLE_POKEMON_PARTY
  if (handlePokemonPartySpriteRefresh()) {
    doFull = true;
  }
#endif
  renderer.displayBuffer(doFull ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    const bool diskCarouselReady = homeUsesCarouselCache() && homecarousel::HomeCarouselCache::shared().keyHash != 0 &&
                                   carouselCoverThumbsReady(recentBooks);
    if (diskCarouselReady) {
      recentsLoaded = true;
    } else {
      loadRecentCovers(metrics.homeCoverHeight);
    }
  }

  if (carouselWarmupPending && !carouselFramesReady) {
    carouselWarmupPending = false;
    const bool showedWarmupProgress = preRenderCarouselFrames(true);
    if (carouselFramesReady || showedWarmupProgress) {
      requestUpdate();
    }
  }
}

void HomeActivity::onContinueReading() { activityManager.goToReader(APP_STATE.openEpubPath); }

void HomeActivity::onMyLibraryOpen() { activityManager.goToMyLibrary(); }

#if ENABLE_BOOKS_TAB_UI
void HomeActivity::onBooksTabOpen() {
  activityManager.replaceActivity(std::make_unique<BooksTabActivity>(renderer, mappedInput));
}
#endif

void HomeActivity::onLibraryOpen() {
  const auto& servers = OPDS_STORE.getServers();
  if (!servers.empty()) {
    activityManager.replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  }
}

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

#if ENABLE_POKEMON_PARTY
bool HomeActivity::handlePokemonPartySpriteRefresh() {
  if (!isPokemonPartyHomeMode() || recentBooks.empty()) {
    return false;
  }

  PokemonPartySprites::RefreshState state{pokemonSpriteRefreshRetries, pokemonSpriteCacheFingerprint};
  const PokemonPartySprites::SyncResult sync = cachedPartySyncResult;
  const PokemonPartySprites::RefreshDecision decision = PokemonPartySprites::decideRefresh(state, sync);
  pokemonSpriteRefreshRetries = state.retries;
  pokemonSpriteCacheFingerprint = state.cacheFingerprint;
  if (decision.requestRedraw) {
    retrySyncPending = true;
  }
  return decision.forceFullRefresh;
}

void HomeActivity::onAssignPokemonOpen() {
  startActivityForResult(std::make_unique<PokemonAssignActivity>(renderer, mappedInput), [this](const ActivityResult&) {
    pokemonSpriteRefreshRetries = 0;
    pokemonSpriteCacheFingerprint = 0;
    retrySyncPending = false;
    runPartySpritesSync();
    requestUpdate();
  });
}

void HomeActivity::runPartySpritesSync() {
  if (isPokemonPartyHomeMode() && !recentBooks.empty()) {
    PokemonPartyTheme::invalidateCache();
    std::vector<RecentBook> deviceBooks;
    deviceBooks.reserve(recentBooks.size());
    for (const auto& book : recentBooks) {
      if (!isOpdsShelfPath(book.path)) {
        deviceBooks.push_back(book);
      }
    }
    cachedPartySyncResult = PokemonPartySprites::syncPartySprites(deviceBooks);
  }
}
#endif

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onTodoOpen() { activityManager.goToTodo(); }

void HomeActivity::onAnkiOpen() { activityManager.goToAnki(); }

#if ENABLE_BOOKMARKS
void HomeActivity::onBookmarksOpen() {
  startActivityForResult(std::make_unique<BookmarksHomeActivity>(renderer, mappedInput),
                         [this](const ActivityResult&) { requestUpdate(); });
}
#endif

void HomeActivity::onNotesOpen() { activityManager.goToNotes(); }

#if ENABLE_LUA_PLUGINS
void HomeActivity::onPluginsOpen() {
  auto onLaunchPlugin = [this](const std::string& name) {
    startActivityForResult(
        std::make_unique<LuaActivity>(renderer, mappedInput, name, [this] { activityManager.popActivity(); }), nullptr);
  };
  startActivityForResult(std::make_unique<PluginListActivity>(renderer, mappedInput, onLaunchPlugin,
                                                              [this] { activityManager.popActivity(); }),
                         nullptr);
}
#endif

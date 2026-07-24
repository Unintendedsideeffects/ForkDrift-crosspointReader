#pragma once
#include <FeatureFlags.h>

#if ENABLE_POKEMON_PARTY
#include "util/PokemonPartySprites.h"
#endif

#include <array>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/RecentBooksStore.h"

struct Rect;
enum class UIIcon : uint8_t;

// Identity for each Home menu entry. The ordered `menuModel` below is the ONE
// source of truth for what the menu contains and in what order. Navigation
// (count), rendering (labels/icons), and activation all index into it, so they
// can never drift apart — which is what previously caused selection misfires
// (carousel last-item unreachable, classic list "Settings" opening File
// Transfer, etc.). Themes only render what they are handed.
enum class HomeMenuId : uint8_t {
  ContinueReading,  // classic list "book card" (slot 0 when a book is open)
  OpenBook,         // carousel: open the centered book
  MyLibrary,
#if ENABLE_BOOKS_TAB_UI
  BooksTab,
#endif
  Library,
  Opds,
  Todo,
  Anki,
  Notes,
  Bookmarks,
  FileTransfer,
#if ENABLE_POKEMON_PARTY
  AssignPokemon,  // Pokémon-party theme only: offline assign of a team member to a book
#endif
  Settings,
#if ENABLE_LUA_PLUGINS
  Plugins,
#endif
};

class HomeActivity final : public Activity {
 public:
  static constexpr int kMaxCachedBooks = 3;

 private:
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  int selectedMenuIndex = 0;
  int selectedBookIndex = 0;
  // Single ordered source of truth for the Home menu (see HomeMenuId). Rebuilt
  // for the current nav mode in buildMenuModel(); every consumer indexes it.
  std::vector<HomeMenuId> menuModel;
  // Compact signature of menuModel's composition, refreshed by buildMenuModel().
  // Folded into the carousel cache key so a menu change invalidates baked frames.
  std::string carouselMenuSignature;
#if ENABLE_BOOKMARKS
  bool hasBookmarks = false;
  void onBookmarksOpen();
#endif

  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool inButtonGrid = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool hasCoverImage = false;
  bool hasContinueReading = false;
  bool updateRequired = false;

  // Carousel state
  int lastCarouselBookIndex = 0;
  bool carouselFramesReady = false;
  bool carouselWarmupPending = false;
  bool bookProgressCached = false;
  std::array<float, kMaxCachedBooks> cachedBookProgress{};

  // Static cover cache — reused while Home is active; freed on exit so other
  // activities (reader, settings) are not starved of heap.
  static bool coverRendered;
  static bool coverBufferStored;
  static uint8_t* coverBuffer;
  static std::vector<std::string> coverCacheBookPaths;
  // Width of the classic-list book card, derived from the cover's aspect ratio
  // on first render. Static so it persists with the static cover buffer above:
  // the buffer-restore selection overlay hugs the same rect the cover was drawn
  // into, even on a fresh HomeActivity instance reusing the cache (0 = no cover
  // / full-box fallback).
  static int classicCoverCardWidth;

  std::string lastBookTitle;
  std::string lastBookAuthor;
  std::string coverBmpPath;
  float currentBookProgressPercent = -1.0f;
  std::vector<RecentBook> recentBooks;
  void onContinueReading();
  void onMyLibraryOpen();
#if ENABLE_BOOKS_TAB_UI
  void onBooksTabOpen();
#endif
  void onLibraryOpen();
  void onNotesOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
#if ENABLE_POKEMON_PARTY
  void onAssignPokemonOpen();
  bool handlePokemonPartySpriteRefresh();
  int pokemonSpriteRefreshRetries = 0;
  uint32_t pokemonSpriteCacheFingerprint = 0;
  PokemonPartySprites::SyncResult cachedPartySyncResult;
  bool retrySyncPending = false;
  void runPartySpritesSync();
#endif
  void onOpdsBrowserOpen();
  void onTodoOpen();
  void onAnkiOpen();
#if ENABLE_LUA_PLUGINS
  void onPluginsOpen();
#endif

  void freeCoverBuffer();  // Free the stored cover buffer
  bool isCoverCacheValid(int coverHeight, bool usesDualSizeCoverThumbs) const;
  void renderCarouselFrameToCurrentBuffer(int bookIdx, float* outProgressPercent);
  void renderCarouselFrame(int bookIdx, int slotIdx);
  void updateSlidingWindowCache(int centerIdx, int bookCount);
  bool preRenderCarouselFrames(bool showProgressPopup = false);
  void loadBookProgress();
  static void renderCarouselFrameCallback(void* context, int bookIdx);
  static void releaseCoverBufferCallback(void* context);

 protected:
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void loadRecentBooks();
  void loadRecentCovers(int coverHeight);
  void openSelectedBook();
  void openCenteredBook();   // carousel: activate the centered book
  void buildMenuModel();     // (re)build menuModel + refresh carouselMenuSignature
  void populateMenuModel();  // fills menuModel for the current nav mode (no signature)
  bool isPokemonPartyHomeMode() const;
  void activateMenuId(HomeMenuId id);
  std::string menuIdLabel(HomeMenuId id, bool gridStyle = false) const;
  UIIcon menuIdIcon(HomeMenuId id) const;
  bool drawCoverAt(const std::string& coverPath, int x, int y, int width, int height) const;

  static std::string fallbackTitleFromPath(const std::string& path);
  static std::string fallbackAuthor(const RecentBook& book);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool blocksBackgroundServer() override;
};

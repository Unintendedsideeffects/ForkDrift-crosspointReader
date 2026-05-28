#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "ControlsOptionsActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    SELECT_CHAPTER,
    CONTROLS_OPTIONS,
    READER_SETTINGS_CHANGED,
    FOOTNOTES,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    TOGGLE_COMPLETED,
    ADD_TO_ANKI,
    DELETE_CACHE,
#if ENABLE_BOOKMARKS
    BOOKMARK_TOGGLE,
    VIEW_BOOKMARKS,
    DELETE_BOOKMARKS,
#endif
  };

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  int currentPage, int totalPages, int bookProgressPercent, uint8_t currentOrientation,
                                  bool hasFootnotes, bool isBookCompleted
#if ENABLE_BOOKMARKS
                                  ,
                                  bool hasBookmarks = false, bool isCurrentPageBookmarked = false
#endif
  );

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool hasFootnotes, bool isBookCompleted
#if ENABLE_BOOKMARKS
                                              ,
                                              bool hasBookmarks, bool isCurrentPageBookmarked
#endif
  );

  const std::vector<MenuItem> menuItems;

  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};

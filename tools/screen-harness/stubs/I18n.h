#pragma once

#include <cstdint>

#include_next <I18nKeys.h>

class I18n {
 public:
  static I18n& getInstance() {
    static I18n instance;
    return instance;
  }

  const char* get(StrId id) const {
    switch (id) {
      case StrId::STR_CROSSPOINT:
        return "CrossPoint";
      case StrId::STR_BOOTING:
        return "Booting...";
      case StrId::STR_SETTINGS_TITLE:
        return "Settings";
      case StrId::STR_CAT_DISPLAY:
        return "Display";
      case StrId::STR_CAT_READER:
        return "Reader";
      case StrId::STR_CAT_CONTROLS:
        return "Controls";
      case StrId::STR_CAT_SYSTEM:
        return "System";
      case StrId::STR_BACK:
        return "Back";
      case StrId::STR_SELECT:
        return "Select";
      case StrId::STR_TOGGLE:
        return "Toggle";
      case StrId::STR_DIR_UP:
        return "Up";
      case StrId::STR_DIR_DOWN:
        return "Down";
      case StrId::STR_STATE_ON:
        return "On";
      case StrId::STR_STATE_OFF:
        return "Off";
      case StrId::STR_UI_THEME:
        return "UI Theme";
      case StrId::STR_THEME_CLASSIC:
        return "Classic";
      case StrId::STR_THEME_LYRA:
        return "Lyra";
      case StrId::STR_THEME_LYRA_EXTENDED:
        return "Visual Covers";
      case StrId::STR_THEME_FORK_DRIFT:
        return "Fork Drift";
      case StrId::STR_THEME_POKEMON_PARTY:
        return "Pokemon Party";
      case StrId::STR_THEME_MINIMAL:
        return "Minimal";
      case StrId::STR_THEME_LYRA_CAROUSEL:
        return "Lyra Carousel";
      case StrId::STR_DARK_MODE:
        return "Dark Mode";
      case StrId::STR_SLEEP_SCREEN:
        return "Sleep Screen";
      case StrId::STR_SLEEP_SOURCE:
        return "Sleep Source";
      case StrId::STR_SLEEP_COVER_MODE:
        return "Sleep Cover Mode";
      case StrId::STR_SLEEP_COVER_FILTER:
        return "Sleep Cover Filter";
      case StrId::STR_SLEEP_CYCLE_MODE:
        return "Sleep Cycle";
      case StrId::STR_VALIDATE_SLEEP_IMAGES:
        return "Validate Sleep Images";
      case StrId::STR_CHAPTER_PAGE_COUNT:
        return "Chapter Page Count";
      case StrId::STR_BOOK_PROGRESS_PERCENTAGE:
        return "Book Progress";
      case StrId::STR_PROGRESS_BAR:
        return "Progress Bar";
      case StrId::STR_PROGRESS_BAR_THICKNESS:
        return "Progress Thickness";
      case StrId::STR_TITLE:
        return "Title";
      case StrId::STR_BATTERY:
        return "Battery";
      case StrId::STR_HIDE_BATTERY:
        return "Hide Battery";
      case StrId::STR_RECENT_BOOKS_VIEW:
        return "Recent Books View";
      case StrId::STR_SUNLIGHT_FADING_FIX:
        return "Sunlight Fading Fix";
      case StrId::STR_FONT_FAMILY:
        return "Font Family";
      case StrId::STR_FONT_SIZE:
        return "Font Size";
      case StrId::STR_LINE_SPACING:
        return "Line Spacing";
      case StrId::STR_SCREEN_MARGIN:
        return "Screen Margin";
      case StrId::STR_REFRESH_FREQ:
        return "Refresh Frequency";
      case StrId::STR_DEVICE_NAME:
        return "Device Name";
      case StrId::STR_DARK:
        return "Dark";
      case StrId::STR_LIGHT:
        return "Light";
      case StrId::STR_FOLLOW_THEME:
        return "Follow Theme";
      case StrId::STR_CUSTOM:
        return "Custom";
      case StrId::STR_TRANSPARENT:
        return "Transparent";
      case StrId::STR_SLEEP_SMART:
        return "Smart";
      case StrId::STR_READING_STATS:
        return "Reading Stats";
      case StrId::STR_SLEEP:
        return "Sleep";
      case StrId::STR_POKEDEX:
        return "Pokedex";
      case StrId::STR_ALL:
        return "All";
      case StrId::STR_FIT:
        return "Fit";
      case StrId::STR_CROP:
        return "Crop";
      case StrId::STR_NONE_OPT:
        return "None";
      case StrId::STR_FILTER_CONTRAST:
        return "Contrast";
      case StrId::STR_INVERTED:
        return "Inverted";
      case StrId::STR_RANDOM:
        return "Random";
      case StrId::STR_SEQUENTIAL:
        return "Sequential";
      case StrId::STR_BOOK:
        return "Book";
      case StrId::STR_CHAPTER:
        return "Chapter";
      case StrId::STR_HIDE:
        return "Hide";
      case StrId::STR_PROGRESS_BAR_THIN:
        return "Thin";
      case StrId::STR_PROGRESS_BAR_MEDIUM:
        return "Medium";
      case StrId::STR_PROGRESS_BAR_THICK:
        return "Thick";
      case StrId::STR_NEVER:
        return "Never";
      case StrId::STR_IN_READER:
        return "In Reader";
      case StrId::STR_ALWAYS:
        return "Always";
      case StrId::STR_PAGES_1:
        return "Every page";
      case StrId::STR_PAGES_5:
        return "Every 5 pages";
      case StrId::STR_PAGES_10:
        return "Every 10 pages";
      case StrId::STR_PAGES_15:
        return "Every 15 pages";
      case StrId::STR_PAGES_30:
        return "Every 30 pages";
      case StrId::STR_LIST_VIEW:
        return "List";
      case StrId::STR_GRID_VIEW:
        return "Grid";
      case StrId::STR_WIFI_NETWORKS:
        return "WiFi Networks";
      case StrId::STR_FILE_TRANSFER:
        return "File Transfer";
      case StrId::STR_FACTORY_RESET:
        return "Factory Reset";
      case StrId::STR_NO_OPEN_BOOK:
        return "No open book";
      case StrId::STR_START_READING:
        return "Start reading to populate this shelf.";
      default:
        return "Setting";
    }
  }

  const char* operator[](StrId id) const { return get(id); }
  Language getLanguage() const { return Language::EN; }
  void setLanguage(Language) {}
  const char* getLanguageName(Language) const { return "English"; }
  static Language languageFromCode(const char*) { return Language::EN; }
  static const char* getCharacterSet(Language) { return ""; }
};

#define tr(id) I18n::getInstance().get(StrId::id)
#define I18N I18n::getInstance()

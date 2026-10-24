#include "I18n.h"

#include <cstddef>
#include <cstring>

#include "I18nStrings.h"

using namespace i18n_strings;

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(const StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::STR_COUNT)) {
    return "???";
  }

  // setLanguage() never stores an unavailable language, but stay safe: its
  // tables are one-element stubs, so indexing them would read past the end.
  const LangStrings lang = getLanguageStrings(isLanguageAvailable(_language) ? _language : Language::EN);

  const uint16_t off = lang.offsets[index];
  if (off == I18N_ENGLISH_FALLBACK_OFFSET) return STRINGS_EN_DATA + OFFSETS_EN[index];
  return lang.data + off;
}

void I18n::setLanguage(const Language lang) {
  if (lang >= Language::LANGUAGE_COUNT || !isLanguageAvailable(lang)) {
    return;
  }
  _language = lang;
}

const char* I18n::getLanguageName(const Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::LANGUAGE_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

Language I18n::languageFromCode(const char* code) {
  for (uint8_t i = 0; i < getLanguageCount(); i++) {
    if (strcmp(code, LANGUAGE_CODES[i]) == 0) return static_cast<Language>(i);
  }
  return Language::EN;
}

const char* I18n::getCharacterSet(const Language lang) {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::LANGUAGE_COUNT)) {
    return CHARACTER_SETS[0];
  }
  return CHARACTER_SETS[index];
}

bool I18n::isLanguageAvailable(const Language lang) {
  const auto index = static_cast<size_t>(lang);
  return index < static_cast<size_t>(Language::LANGUAGE_COUNT) && LANGUAGE_AVAILABLE[index];
}

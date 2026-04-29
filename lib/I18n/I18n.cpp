#include "I18n.h"

#include <HalStorage.h>
#include <Serialization.h>

#include "I18nStrings.h"

using namespace i18n_strings;

namespace {
constexpr const char* SETTINGS_FILE = "/.crosspoint/language.bin";
constexpr uint8_t SETTINGS_VERSION = 1;
}  // namespace

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(const StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  const char* const* strings = getStringArray(_language);
  return strings[index];
}

void I18n::setLanguage(const Language lang) {
  if (lang >= Language::_COUNT) {
    return;
  }
  _language = lang;
  saveSettings();
}

const char* I18n::getLanguageName(const Language lang) const {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return "???";
  }
  return LANGUAGE_NAMES[index];
}

void I18n::saveSettings() {
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("I18N", SETTINGS_FILE, file)) {
    return;
  }

  serialization::writePod(file, SETTINGS_VERSION);
  serialization::writePod(file, static_cast<uint8_t>(_language));
  file.close();
}

void I18n::loadSettings() {
  FsFile file;
  if (!Storage.openFileForRead("I18N", SETTINGS_FILE, file)) {
    return;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != SETTINGS_VERSION) {
    Serial.printf("[I18N] Settings version mismatch\n");
    return;
  }

  uint8_t languageValue = 0;
  if (!serialization::readPod(file, languageValue)) {
    file.close();
    return;
  }

  if (languageValue < static_cast<uint8_t>(Language::_COUNT)) {
    _language = static_cast<Language>(languageValue);
  }
}

const char* I18n::getCharacterSet(const Language lang) {
  const auto index = static_cast<size_t>(lang);
  if (index >= static_cast<size_t>(Language::_COUNT)) {
    return CHARACTER_SETS[0];
  }
  return CHARACTER_SETS[index];
}

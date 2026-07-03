#pragma once

#include <FeatureFlags.h>

#if ENABLE_PER_BOOK_SETTINGS

#include <cstdint>
#include <string>

// Per-book reader-setting overrides. Inspired by the Inx firmware's per-book
// settings (github.com/obijuankenobiii/inx, MIT), implemented natively: when a
// book with overrides opens, the set fields are written into the global
// SETTINGS after snapshotting, and the snapshot is restored on exit without
// touching the settings file on disk. Every downstream consumer — renderer,
// section cache keys, options overlay — works unchanged.
//
// Persisted as <book cache dir>/book_settings.json.
struct BookSettingsOverride {
  bool enabled = false;  // the "For this book only" toggle

  // Set-bits: only fields the user changed while the toggle was on apply.
  bool hasFontFamily = false;
  bool hasFontSize = false;
  bool hasLineSpacing = false;
  bool hasScreenMargin = false;
  bool hasParagraphAlignment = false;
  bool hasExtraParagraphSpacing = false;
  bool hasForceParagraphIndents = false;
  bool hasHyphenation = false;
  bool hasTextAntiAliasing = false;
  bool hasImageRendering = false;

  uint8_t fontFamily = 0;
  char sdFontFamilyName[32] = "";
  uint8_t fontSize = 0;
  uint8_t lineSpacing = 0;
  uint8_t screenMargin = 0;
  uint8_t paragraphAlignment = 0;
  uint8_t extraParagraphSpacing = 0;
  uint8_t forceParagraphIndents = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t textAntiAliasing = 0;
  uint8_t imageRendering = 0;

  bool anySet() const {
    return hasFontFamily || hasFontSize || hasLineSpacing || hasScreenMargin || hasParagraphAlignment ||
           hasExtraParagraphSpacing || hasForceParagraphIndents || hasHyphenation || hasTextAntiAliasing ||
           hasImageRendering;
  }

  static bool load(const std::string& cachePath, BookSettingsOverride& out);
  bool save(const std::string& cachePath) const;
  static void remove(const std::string& cachePath);

  // Capture the current global values for every SET field (snapshot before
  // applying), or write this override's set fields into SETTINGS (apply).
  // captureFromGlobals copies globals for ALL fields so a restore is complete.
  void captureFromGlobals();
  void applyToGlobals() const;
};

// Registration of the currently-open book's override so the reader options
// overlay (launched two activities deep) can record changes without plumbing
// pointers through the menu. Set by EpubReaderActivity for its lifetime.
namespace BookSettingsScope {
// `snapshot` holds the pre-apply global values (captureFromGlobals before
// applyToGlobals); needed so global saves during per-book mode can persist
// without baking per-book values into the global settings file.
void setActive(BookSettingsOverride* override, const std::string& cachePath, const BookSettingsOverride& snapshot);
// Save the global settings file while per-book overrides are applied in RAM:
// temporarily restores the snapshot values, saves, then re-applies overrides.
void saveGlobalsPreservingOverrides();
void clearActive();
bool isActive();
BookSettingsOverride* active();
// Copy the CURRENT global value of the setting identified by `key` into the
// active override (marking it set) and persist the override file. Returns
// false when the key is not per-book-capable (caller falls back to the global
// save).
bool recordChange(const char* key);
// Toggle per-book mode: enabling persists the override file; disabling deletes
// it and reloads true global settings over the per-book values in RAM.
void setEnabled(bool enabled);
bool isEnabled();
}  // namespace BookSettingsScope

#endif  // ENABLE_PER_BOOK_SETTINGS

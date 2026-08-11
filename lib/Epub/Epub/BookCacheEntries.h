#pragma once

#include <cstring>

// Which entries under a book's cache directory (`.crosspoint/epub_<hash>/`)
// belong to the reader rather than to the renderer.
//
// Everything under that directory is regenerable from the book file *except*
// these, so when the book file changes we delete the rest and keep these.
// Splitting the policy out of Epub.cpp keeps it host-testable: the failure mode
// worth guarding is a new per-book store being added without being listed, and
// that is a question about this list, not about the SD card.
namespace book_cache {

// IF YOU ADD A NEW PER-BOOK STORE UNDER THE CACHE DIRECTORY, ADD IT HERE.
// A store missing from this list is silently deleted the first time the user
// re-uploads or re-downloads the book.
inline constexpr const char* kUserStateEntries[] = {
    "progress.bin",        // BookProgressDataStore — reading position
    "annotations.bin",     // AnnotationStore — highlights and notes
    "book_settings.json",  // BookSettingsOverride — per-book settings
    "pokemon.json",        // PokemonBookDataStore — per-book party
};

// True if `name` (a bare entry name, not a path) is reader-owned data that must
// survive invalidation of the derived cache.
inline bool isUserStateEntry(const char* name) {
  if (name == nullptr) return false;
  for (const char* keep : kUserStateEntries) {
    if (std::strcmp(name, keep) == 0) return true;
  }
  return false;
}

}  // namespace book_cache

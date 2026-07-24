#pragma once

#include <FeatureFlags.h>

// Exporting highlights/bookmarks depends on there being something to export.
#if ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

#include <cstdint>
#include <string>
#include <vector>

#if ENABLE_ANNOTATIONS
#include "util/AnnotationStore.h"  // Annotation
#endif
#include "BookmarkStore.h"  // Bookmark

namespace highlight_export {

// User-pickable export formats (persisted as CrossPointSettings::highlightExportFormat).
// Keep values stable — they are written to the settings file.
enum Format : uint8_t {
  FORMAT_MARKDOWN = 0,      // one .md per book: highlights as blockquotes, bookmarks listed
  FORMAT_MY_CLIPPINGS = 1,  // Kindle "My Clippings.txt" (appended); de-facto interop format
  FORMAT_KOREADER = 2,      // KOReader .sdr/metadata sidecar (Lua); round-trips with KOReader
  FORMAT_COUNT = 3,
};

// One book's exportable data, gathered from the stores by the caller.
struct BookExport {
  std::string title;
  std::string author;
  std::string path;  // full book path on SD (used to derive sidecar/output paths)
#if ENABLE_ANNOTATIONS
  std::vector<Annotation> highlights;
#endif
#if ENABLE_BOOKMARKS
  std::vector<Bookmark> bookmarks;
#endif
};

// Pure serializers (no I/O) — the file content for the given format. Host-testable.
std::string serializeMarkdown(const BookExport& book);
std::string serializeMyClippings(const BookExport& book);
std::string serializeKoreader(const BookExport& book);

// Serialize `book` in `format` and write it to the format's conventional location
// on the SD card. Returns the output path, or empty on failure.
//   Markdown     -> /Notes/<title>.md            (overwrite)
//   My Clippings -> /My Clippings.txt            (append)
//   KOReader     -> <book>.sdr/metadata.<ext>.lua (overwrite)
std::string exportBook(Format format, const BookExport& book);

// True when there is anything to export.
bool hasContent(const BookExport& book);

}  // namespace highlight_export

#endif  // ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

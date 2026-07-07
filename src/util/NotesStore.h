#pragma once

#include <string>

// Per-book reading notes: highlights appended as Markdown blockquotes to
// /Notes/<book title>.md on the SD card. Human-readable and trivially
// syncable; a KOReader-sidecar exporter can be layered on the same data.
namespace NotesStore {

// Append one highlight. `location` is a short human-readable position
// ("Chapter 3, p12, 45%"). Returns false on any storage failure.
bool appendHighlight(const std::string& bookTitle, const std::string& location, const std::string& text);

}  // namespace NotesStore

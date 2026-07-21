#pragma once

#include <string>

// Per-book reading notes: highlights appended as Markdown blockquotes to
// /Notes/<book title>.md on the SD card. Human-readable and trivially
// syncable; a KOReader-sidecar exporter can be layered on the same data.
namespace NotesStore {

std::string bookNotesPath(const std::string& bookTitle);
bool appendHighlight(const std::string& bookTitle, const std::string& location, const std::string& text,
                     std::string* outPath = nullptr);

}  // namespace NotesStore

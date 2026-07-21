#include "NotesStore.h"

#include <FeatureFlags.h>

#if ENABLE_TEXT_SELECTION

#include <HalStorage.h>
#include <Logging.h>

namespace {

// FAT-safe filename from the book title (fallback for empty results).
std::string sanitizeTitle(const std::string& title) {
  std::string out;
  out.reserve(title.size());
  for (const char c : title) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
                    c == '-' || c == '_' || c == '.';
    out += ok ? c : '_';
    if (out.size() >= 60) break;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
    out.pop_back();
  }
  return out.empty() ? std::string("untitled") : out;
}

}  // namespace

namespace NotesStore {

std::string bookNotesPath(const std::string& bookTitle) { return "/Notes/" + sanitizeTitle(bookTitle) + ".md"; }

bool appendHighlight(const std::string& bookTitle, const std::string& location, const std::string& text,
                     std::string* outPath) {
  if (text.empty()) {
    return false;
  }
  if (!Storage.mkdir("/Notes")) {
    LOG_DBG("NOTES", "mkdir /Notes returned false (may already exist)");
  }

  const std::string path = bookNotesPath(bookTitle);
  const bool isNew = !Storage.exists(path.c_str());

  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("NOTES", "Failed to open %s", path.c_str());
    return false;
  }

  std::string entry;
  entry.reserve(text.size() + location.size() + bookTitle.size() + 32);
  if (isNew) {
    entry += "# ";
    entry += bookTitle.empty() ? "Notes" : bookTitle;
    entry += "\n\n";
  }
  entry += "> ";
  entry += text;
  entry += "\n>\n> \xe2\x80\x94 ";  // em dash
  entry += location;
  entry += "\n\n";

  const size_t written = file.write(reinterpret_cast<const uint8_t*>(entry.data()), entry.size());
  if (written != entry.size()) {
    LOG_ERR("NOTES", "Short write to %s (%u/%u)", path.c_str(), static_cast<unsigned>(written),
            static_cast<unsigned>(entry.size()));
    return false;
  }
  LOG_INF("NOTES", "Highlight saved to %s", path.c_str());
  if (outPath) {
    *outPath = path;
  }
  return true;
}

}  // namespace NotesStore
#endif  // ENABLE_TEXT_SELECTION

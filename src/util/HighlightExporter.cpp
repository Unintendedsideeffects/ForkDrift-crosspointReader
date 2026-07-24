#include "util/HighlightExporter.h"

#if ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <ctime>

#include "util/NotesStore.h"

namespace highlight_export {
namespace {

// Format a unix timestamp as "YYYY-MM-DD HH:MM:SS" (UTC). Empty for 0/unset.
std::string formatTimestamp(uint32_t timestamp) {
  if (timestamp == 0) {
    return "";
  }
  const std::time_t t = static_cast<std::time_t>(timestamp);
  std::tm tmValue{};
#if defined(_WIN32)
  gmtime_s(&tmValue, &t);
#else
  gmtime_r(&t, &tmValue);
#endif
  char buf[24];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmValue);
  return buf;
}

// Escape a string for a double-quoted Lua literal.
std::string luaEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (const char c : in) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
    }
  }
  return out;
}

// Derive the KOReader sidecar path from a book path: /Books/foo.epub ->
// /Books/foo.sdr/metadata.epub.lua (ext without the dot; "epub" fallback).
std::string koreaderSidecarPath(const std::string& bookPath) {
  const size_t slash = bookPath.find_last_of('/');
  const size_t dot = bookPath.find_last_of('.');
  const bool hasExt = dot != std::string::npos && (slash == std::string::npos || dot > slash);
  const std::string stem = hasExt ? bookPath.substr(0, dot) : bookPath;
  const std::string ext = hasExt ? bookPath.substr(dot + 1) : std::string("epub");
  return stem + ".sdr/metadata." + ext + ".lua";
}

bool writeWhole(const std::string& path, const std::string& content) {
  HalFile file;
  if (!Storage.openFileForWrite("EXPORT", path, file)) {
    LOG_ERR("EXPORT", "Failed to open for write: %s", path.c_str());
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  file.close();
  return written == content.size();
}

bool appendWhole(const std::string& path, const std::string& content) {
  HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("EXPORT", "Failed to open for append: %s", path.c_str());
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  file.close();
  return written == content.size();
}

}  // namespace

bool hasContent(const BookExport& book) {
  size_t count = 0;
#if ENABLE_ANNOTATIONS
  count += book.highlights.size();
#endif
#if ENABLE_BOOKMARKS
  count += book.bookmarks.size();
#endif
  return count > 0;
}

std::string serializeMarkdown(const BookExport& book) {
  std::string out;
  out += "# " + book.title + "\n";
  if (!book.author.empty()) {
    out += "*" + book.author + "*\n";
  }
  out += "\n";

#if ENABLE_ANNOTATIONS
  if (!book.highlights.empty()) {
    out += "## Highlights\n\n";
    for (const auto& h : book.highlights) {
      // Blockquote each line so multi-line highlights stay quoted.
      std::string text = h.text;
      out += "> ";
      for (const char c : text) {
        out += c;
        if (c == '\n') {
          out += "> ";
        }
      }
      out += "\n\n";
    }
  }
#endif

#if ENABLE_BOOKMARKS
  if (!book.bookmarks.empty()) {
    out += "## Bookmarks\n\n";
    for (const auto& b : book.bookmarks) {
      char pct[8];
      snprintf(pct, sizeof(pct), "%.0f%%", b.progress * 100.0f);
      const std::string chapter = b.chapterTitle[0] ? std::string(b.chapterTitle) : std::string("(no chapter)");
      out += "- " + chapter + " — " + pct + "\n";
    }
    out += "\n";
  }
#endif
  return out;
}

std::string serializeMyClippings(const BookExport& book) {
  const std::string header = book.title + (book.author.empty() ? "" : " (" + book.author + ")");
  const std::string sep = "==========\n";
  std::string out;

#if ENABLE_ANNOTATIONS
  for (const auto& h : book.highlights) {
    out += header + "\n";
    char loc[48];
    snprintf(loc, sizeof(loc), "- Your Highlight | Location %u\n\n", static_cast<unsigned>(h.spineIndex));
    out += loc;
    out += h.text + "\n";
    out += sep;
  }
#endif

#if ENABLE_BOOKMARKS
  for (const auto& b : book.bookmarks) {
    out += header + "\n";
    const std::string date = formatTimestamp(b.timestamp);
    char loc[80];
    snprintf(loc, sizeof(loc), "- Your Bookmark | Location %u%s%s\n\n", static_cast<unsigned>(b.spineIndex),
             date.empty() ? "" : " | Added on ", date.c_str());
    out += loc;
    if (b.chapterTitle[0]) {
      out += std::string(b.chapterTitle) + "\n";
    }
    out += sep;
  }
#endif
  return out;
}

std::string serializeKoreader(const BookExport& book) {
  // Best-effort KOReader-compatible sidecar. Highlights are grouped by a page
  // key (spineIndex here) as KOReader expects a nested [page][idx] table.
  std::string out = "-- exported by CrossPoint Reader\nreturn {\n";

#if ENABLE_ANNOTATIONS
  out += "  [\"highlight\"] = {\n";
  int idx = 1;
  for (const auto& h : book.highlights) {
    out += "    [" + std::to_string(idx) + "] = {\n";
    out += "      [1] = {\n";
    out += "        [\"text\"] = \"" + luaEscape(h.text) + "\",\n";
    out += "        [\"page\"] = " + std::to_string(h.spineIndex) + ",\n";
    out += "      },\n";
    out += "    },\n";
    ++idx;
  }
  out += "  },\n";
#endif

#if ENABLE_BOOKMARKS
  out += "  [\"bookmarks\"] = {\n";
  int bidx = 1;
  for (const auto& b : book.bookmarks) {
    const std::string date = formatTimestamp(b.timestamp);
    out += "    [" + std::to_string(bidx) + "] = {\n";
    out += "      [\"page\"] = " + std::to_string(b.spineIndex) + ",\n";
    if (b.chapterTitle[0]) {
      out += "      [\"chapter\"] = \"" + luaEscape(b.chapterTitle) + "\",\n";
    }
    if (!date.empty()) {
      out += "      [\"datetime\"] = \"" + date + "\",\n";
    }
    out += "    },\n";
    ++bidx;
  }
  out += "  },\n";
#endif

  out += "}\n";
  return out;
}

std::string exportBook(Format format, const BookExport& book) {
  if (!hasContent(book)) {
    return "";
  }
  switch (format) {
    case FORMAT_MY_CLIPPINGS: {
      const std::string path = "/My Clippings.txt";
      return appendWhole(path, serializeMyClippings(book)) ? path : std::string();
    }
    case FORMAT_KOREADER: {
      const std::string path = koreaderSidecarPath(book.path);
      const size_t slash = path.find_last_of('/');
      if (slash != std::string::npos) {
        Storage.mkdir(path.substr(0, slash).c_str());  // create the .sdr dir
      }
      return writeWhole(path, serializeKoreader(book)) ? path : std::string();
    }
    case FORMAT_MARKDOWN:
    default: {
      Storage.mkdir("/Notes");
      const std::string path = NotesStore::bookNotesPath(book.title);
      return writeWhole(path, serializeMarkdown(book)) ? path : std::string();
    }
  }
}

}  // namespace highlight_export

#endif  // ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

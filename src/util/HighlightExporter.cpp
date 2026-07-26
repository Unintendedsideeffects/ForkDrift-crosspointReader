#include "util/HighlightExporter.h"

#if ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <ctime>

#include "util/NotesStore.h"

namespace highlight_export {
namespace {

// First line of every sidecar this firmware writes. Its presence is what makes a
// sidecar safe to overwrite: a file without it belongs to KOReader and holds
// reading position, per-book settings and statistics we cannot reproduce.
constexpr const char* kKoreaderProvenance = "-- exported by CrossPoint Reader";

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

// True when `path` does not exist, or exists and begins with our provenance
// marker. False means the file is KOReader's own sidecar and must be left alone.
bool sidecarIsOursOrAbsent(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("EXPORT", path.c_str(), file)) {
    return true;  // absent (or unreadable) — nothing to destroy
  }
  uint8_t head[64] = {0};
  const int rd = file.read(head, sizeof(head) - 1);
  if (rd <= 0) {
    return true;  // empty file — safe to replace
  }
  return std::strncmp(reinterpret_cast<const char*>(head), kKoreaderProvenance, std::strlen(kKoreaderProvenance)) == 0;
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
  if (book.highlights != nullptr) {
    count += book.highlights->size();
  }
#endif
#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr) {
    count += book.bookmarks->size();
  }
#endif
  return count > 0;
}

// Appenders for single entries / headers
void appendMarkdownHeader(std::string& out, const BookExport& book) {
  out += "# " + book.title + "\n";
  if (!book.author.empty()) {
    out += "*" + book.author + "*\n";
  }
  out += "\n";
}

#if ENABLE_ANNOTATIONS
void appendMarkdownHighlight(std::string& out, const Annotation& h) {
  out += "> ";
  for (const char c : h.text) {
    out += c;
    if (c == '\n') {
      out += "> ";
    }
  }
  out += "\n\n";
}
#endif

#if ENABLE_BOOKMARKS
void appendMarkdownBookmark(std::string& out, const Bookmark& b) {
  char pct[8];
  snprintf(pct, sizeof(pct), "%.0f%%", b.progress * 100.0f);
  const std::string chapter = b.chapterTitle[0] ? std::string(b.chapterTitle) : std::string("(no chapter)");
  out += "- " + chapter + " — " + pct + "\n";
}
#endif

std::string serializeMarkdown(const BookExport& book) {
  std::string out;
  size_t estimate = book.title.size() + book.author.size() + 64;
#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr && !book.highlights->empty()) {
    estimate += 32;
    for (const auto& h : *book.highlights) {
      estimate += h.text.size() + 16;
    }
  }
#endif
#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr && !book.bookmarks->empty()) {
    estimate += 32;
    for (const auto& b : *book.bookmarks) {
      estimate += sizeof(b.chapterTitle) + 32;
    }
  }
#endif
  out.reserve(estimate);

  appendMarkdownHeader(out, book);

#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr && !book.highlights->empty()) {
    out += "## Highlights\n\n";
    for (const auto& h : *book.highlights) {
      appendMarkdownHighlight(out, h);
    }
  }
#endif

#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr && !book.bookmarks->empty()) {
    out += "## Bookmarks\n\n";
    for (const auto& b : *book.bookmarks) {
      appendMarkdownBookmark(out, b);
    }
    out += "\n";
  }
#endif
  return out;
}

#if ENABLE_ANNOTATIONS
void appendMyClippingsHighlight(std::string& out, const std::string& header, const Annotation& h) {
  out += header + "\n";
  char loc[48];
  snprintf(loc, sizeof(loc), "- Your Highlight | Location %u\n\n", static_cast<unsigned>(h.spineIndex));
  out += loc;
  out += h.text + "\n";
  out += "==========\n";
}
#endif

#if ENABLE_BOOKMARKS
void appendMyClippingsBookmark(std::string& out, const std::string& header, const Bookmark& b) {
  out += header + "\n";
  const std::string date = formatTimestamp(b.timestamp);
  char loc[80];
  snprintf(loc, sizeof(loc), "- Your Bookmark | Location %u%s%s\n\n", static_cast<unsigned>(b.spineIndex),
           date.empty() ? "" : " | Added on ", date.c_str());
  out += loc;
  if (b.chapterTitle[0]) {
    out += std::string(b.chapterTitle) + "\n";
  }
  out += "==========\n";
}
#endif

std::string serializeMyClippings(const BookExport& book) {
  const std::string header = book.title + (book.author.empty() ? "" : " (" + book.author + ")");
  std::string out;
  size_t estimate = 0;
#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr) {
    for (const auto& h : *book.highlights) {
      estimate += header.size() + h.text.size() + 80;
    }
  }
#endif
#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr) {
    for (const auto& b : *book.bookmarks) {
      estimate += header.size() + sizeof(b.chapterTitle) + 120;
    }
  }
#endif
  out.reserve(estimate);

#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr) {
    for (const auto& h : *book.highlights) {
      appendMyClippingsHighlight(out, header, h);
    }
  }
#endif

#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr) {
    for (const auto& b : *book.bookmarks) {
      appendMyClippingsBookmark(out, header, b);
    }
  }
#endif
  return out;
}

#if ENABLE_ANNOTATIONS
void appendKoreaderHighlight(std::string& out, int idx, const Annotation& h) {
  out += "    [" + std::to_string(idx) + "] = {\n";
  out += "      [1] = {\n";
  out += "        [\"text\"] = \"" + luaEscape(h.text) + "\",\n";
  out += "        [\"page\"] = " + std::to_string(h.spineIndex) + ",\n";
  out += "      },\n";
  out += "    },\n";
}
#endif

#if ENABLE_BOOKMARKS
void appendKoreaderBookmark(std::string& out, int bidx, const Bookmark& b) {
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
}
#endif

std::string serializeKoreader(const BookExport& book) {
  std::string out;
  size_t estimate = 128;
#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr) {
    for (const auto& h : *book.highlights) {
      estimate += h.text.size() + 128;
    }
  }
#endif
#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr) {
    for (const auto& b : *book.bookmarks) {
      estimate += sizeof(b.chapterTitle) + 128;
    }
  }
#endif
  out.reserve(estimate);

  out += std::string(kKoreaderProvenance) + "\nreturn {\n";

#if ENABLE_ANNOTATIONS
  if (book.highlights != nullptr) {
    out += "  [\"highlight\"] = {\n";
    int idx = 1;
    for (const auto& h : *book.highlights) {
      appendKoreaderHighlight(out, idx, h);
      ++idx;
    }
    out += "  },\n";
  }
#endif

#if ENABLE_BOOKMARKS
  if (book.bookmarks != nullptr) {
    out += "  [\"bookmarks\"] = {\n";
    int bidx = 1;
    for (const auto& b : *book.bookmarks) {
      appendKoreaderBookmark(out, bidx, b);
      ++bidx;
    }
    out += "  },\n";
  }
#endif

  out += "}\n";
  return out;
}

ExportStatus exportBook(Format format, const BookExport& book, std::string& outPath) {
  outPath.clear();
  if (!hasContent(book)) {
    return ExportStatus::Failed;
  }
  switch (format) {
    case FORMAT_MY_CLIPPINGS: {
      const std::string path = "/My Clippings.txt";
      HalFile file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
      if (!file) {
        LOG_ERR("EXPORT", "Failed to open for append: %s", path.c_str());
        return ExportStatus::Failed;
      }
      const std::string header = book.title + (book.author.empty() ? "" : " (" + book.author + ")");
      std::string chunk;
      chunk.reserve(512);

#if ENABLE_ANNOTATIONS
      if (book.highlights != nullptr) {
        for (const auto& h : *book.highlights) {
          chunk.clear();
          appendMyClippingsHighlight(chunk, header, h);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
        }
      }
#endif

#if ENABLE_BOOKMARKS
      if (book.bookmarks != nullptr) {
        for (const auto& b : *book.bookmarks) {
          chunk.clear();
          appendMyClippingsBookmark(chunk, header, b);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
        }
      }
#endif
      file.close();
      outPath = path;
      return ExportStatus::Ok;
    }
    case FORMAT_KOREADER: {
      const std::string path = koreaderSidecarPath(book.path);
      if (!sidecarIsOursOrAbsent(path)) {
        LOG_ERR("EXPORT", "Refusing to overwrite a foreign KOReader sidecar: %s", path.c_str());
        return ExportStatus::SidecarNotOurs;
      }
      const size_t slash = path.find_last_of('/');
      if (slash != std::string::npos) {
        Storage.mkdir(path.substr(0, slash).c_str());  // create the .sdr dir
      }
      HalFile file;
      if (!Storage.openFileForWrite("EXPORT", path, file)) {
        LOG_ERR("EXPORT", "Failed to open for write: %s", path.c_str());
        return ExportStatus::Failed;
      }
      std::string chunk;
      chunk.reserve(512);

      chunk = std::string(kKoreaderProvenance) + "\nreturn {\n";
#if ENABLE_ANNOTATIONS
      if (book.highlights != nullptr) {
        chunk += "  [\"highlight\"] = {\n";
      }
#endif
      if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
        LOG_ERR("EXPORT", "Short write: %s", path.c_str());
        file.close();
        return ExportStatus::Failed;
      }

#if ENABLE_ANNOTATIONS
      if (book.highlights != nullptr) {
        int idx = 1;
        for (const auto& h : *book.highlights) {
          chunk.clear();
          appendKoreaderHighlight(chunk, idx, h);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
          ++idx;
        }
        chunk = "  },\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
      }
#endif

#if ENABLE_BOOKMARKS
      if (book.bookmarks != nullptr) {
        chunk = "  [\"bookmarks\"] = {\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
        int bidx = 1;
        for (const auto& b : *book.bookmarks) {
          chunk.clear();
          appendKoreaderBookmark(chunk, bidx, b);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
          ++bidx;
        }
        chunk = "  },\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
      }
#endif

      chunk = "}\n";
      if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
        LOG_ERR("EXPORT", "Short write: %s", path.c_str());
        file.close();
        return ExportStatus::Failed;
      }
      file.close();
      outPath = path;
      return ExportStatus::Ok;
    }
    case FORMAT_MARKDOWN:
    default: {
      Storage.mkdir("/Notes");
      const std::string path = NotesStore::bookNotesPath(book.title);
      HalFile file;
      if (!Storage.openFileForWrite("EXPORT", path, file)) {
        LOG_ERR("EXPORT", "Failed to open for write: %s", path.c_str());
        return ExportStatus::Failed;
      }
      std::string chunk;
      chunk.reserve(512);

      appendMarkdownHeader(chunk, book);
      if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
        LOG_ERR("EXPORT", "Short write: %s", path.c_str());
        file.close();
        return ExportStatus::Failed;
      }

#if ENABLE_ANNOTATIONS
      if (book.highlights != nullptr && !book.highlights->empty()) {
        chunk = "## Highlights\n\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
        for (const auto& h : *book.highlights) {
          chunk.clear();
          appendMarkdownHighlight(chunk, h);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
        }
      }
#endif

#if ENABLE_BOOKMARKS
      if (book.bookmarks != nullptr && !book.bookmarks->empty()) {
        chunk = "## Bookmarks\n\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
        for (const auto& b : *book.bookmarks) {
          chunk.clear();
          appendMarkdownBookmark(chunk, b);
          if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
            LOG_ERR("EXPORT", "Short write: %s", path.c_str());
            file.close();
            return ExportStatus::Failed;
          }
        }
        chunk = "\n";
        if (file.write(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()) != chunk.size()) {
          LOG_ERR("EXPORT", "Short write: %s", path.c_str());
          file.close();
          return ExportStatus::Failed;
        }
      }
#endif
      file.close();
      outPath = path;
      return ExportStatus::Ok;
    }
  }
}

}  // namespace highlight_export

#endif  // ENABLE_ANNOTATIONS || ENABLE_BOOKMARKS

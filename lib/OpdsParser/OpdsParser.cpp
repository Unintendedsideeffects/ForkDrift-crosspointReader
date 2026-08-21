#include "OpdsParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>

#include <cstring>

namespace {
// Maximum entries stored in memory during feed parsing (bounds memory on large catalogs)
constexpr size_t ENTRY_STORAGE_CAPACITY = 64;
// Limit active entries to 62 to reserve display slots for navigation/pagination items
constexpr size_t MAX_ENTRIES = ENTRY_STORAGE_CAPACITY - 2;
// String field limits prevent malicious or oversized feed fields from exhausting heap
constexpr size_t MAX_TITLE_CHARS = 160;
constexpr size_t MAX_AUTHOR_CHARS = 120;
constexpr size_t MAX_ID_CHARS = 128;
constexpr size_t MAX_HREF_CHARS = 768;
constexpr size_t MAX_SEARCH_TEMPLATE_CHARS = 768;
constexpr size_t MAX_PAGE_URL_CHARS = 768;

// Total bytes accepted into expat across the whole feed. This is the one bound
// that actually protects the heap: expat's XML_GetBuffer must hold a complete,
// contiguous attribute value before startElement ever fires, so a hostile feed
// with e.g. <link href="AAAA...(many MB, unterminated)..." keeps growing
// expat's *own* internal buffer on every write() call, regardless of
// MAX_HREF_CHARS below -- that cap only ever sees the value after expat has
// already assembled the whole (potentially huge) thing. Bounding the feed body
// itself is the only way to bound that upstream allocation on a 380KB,
// no-PSRAM device. Reuses the 64KB "cap an HTTP body pulled fully into memory"
// convention already established for the sibling OpenSearch description-doc
// fetch (HttpDownloader.cpp's BoundedStringSink / kMaxBodyBytes) rather than
// inventing a new number.
constexpr size_t MAX_FEED_BODY_BYTES = 64u * 1024u;
}  // namespace

OpdsParser::OpdsParser() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    errorOccured = true;
    LOG_DBG("OPDS", "Couldn't allocate memory for parser");
    return;
  }
  entries.reserve(ENTRY_STORAGE_CAPACITY);
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
}

OpdsParser::~OpdsParser() { destroyXmlParser(parser); }

size_t OpdsParser::write(uint8_t c) { return write(&c, 1); }

size_t OpdsParser::write(const uint8_t* xmlData, const size_t length) {
  if (errorOccured || !parser) return length;

  const char* currentPos = reinterpret_cast<const char*>(xmlData);
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    // Checked before every chunk (not just once) so a feed that crosses the
    // cap mid-attribute-value is stopped as soon as possible, rather than
    // after this whole write() call's data has already been handed to expat.
    if (bytesFed >= MAX_FEED_BODY_BYTES) {
      LOG_DBG("OPDS", "Feed body exceeded %zu-byte cap; aborting parse", MAX_FEED_BODY_BYTES);
      feedTruncated = true;
      destroyXmlParser(parser);
      return length;
    }

    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;
    void* const buf = XML_GetBuffer(parser, toRead);
    if (!buf) {
      errorOccured = true;
      LOG_DBG("OPDS", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return length;
    }

    memcpy(buf, currentPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), 0) == XML_STATUS_ERROR) {
      errorOccured = true;
      LOG_DBG("OPDS", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return length;
    }
    bytesFed += toRead;
    currentPos += toRead;
    remaining -= toRead;
  }
  return length;
}

void OpdsParser::flush() {
  if (errorOccured || !parser) return;
  if (XML_Parse(parser, nullptr, 0, XML_TRUE) != XML_STATUS_OK) {
    errorOccured = true;
    destroyXmlParser(parser);
  }
}

bool OpdsParser::error() const { return errorOccured; }

void OpdsParser::clear() {
  entries.clear();
  searchTemplate.clear();
  searchDescriptionUrl.clear();
  nextPageUrl.clear();
  prevPageUrl.clear();
  currentEntry = OpdsEntry{};
  currentText.clear();
  inEntry = inTitle = inAuthor = inAuthorName = inId = false;
  collectCurrentEntry = false;
  entriesSeen = 0;
  bytesFed = 0;
  feedTruncated = false;
}

const char* OpdsParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void OpdsParser::assignBounded(std::string& target, const char* value, const size_t maxLen) {
  if (!value) {
    target.clear();
    return;
  }
  target.assign(value, strnlen(value, maxLen));
}

void OpdsParser::appendBounded(std::string& target, const char* value, const size_t len, const size_t maxLen) {
  if (target.size() >= maxLen) return;
  const size_t remaining = maxLen - target.size();
  target.append(value, len < remaining ? len : remaining);
}

void OpdsParser::assignBoundedOrReject(std::string& target, const char* value, const size_t maxLen,
                                       const char* fieldName) {
  if (!value) {
    target.clear();
    return;
  }
  // strnlen(value, maxLen + 1), not strlen: bounds the scan itself so a
  // pathological value can't cost more than maxLen+1 bytes of work here --
  // MAX_FEED_BODY_BYTES already bounds how large value can ever be, but this
  // keeps the guarantee local rather than relying on that invariant holding.
  const size_t probeLen = strnlen(value, maxLen + 1);
  if (probeLen > maxLen) {
    LOG_DBG("OPDS", "Rejecting %s: exceeds %zu-char cap", fieldName, maxLen);
    target.clear();
    return;
  }
  target.assign(value, probeLen);
}

void XMLCALL OpdsParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (xmlNameMatches(name, "entry")) {
    self->inEntry = true;
    // Counted structurally at the open tag, not derived from entries.size():
    // an <entry> that never ends up with both a title and an href (e.g. a
    // feed of <entry><title>x</title></entry> with no link) never gets
    // pushed to `entries`, so gating on entries.size() alone would let
    // collectCurrentEntry stay true forever and tag-parse an unbounded
    // number of entries.
    ++self->entriesSeen;
    self->collectCurrentEntry = self->entriesSeen <= MAX_ENTRIES;
    if (!self->collectCurrentEntry && !self->feedTruncated) {
      LOG_DBG("OPDS", "Feed entries truncated at capacity %zu", MAX_ENTRIES);
    }
    self->feedTruncated = self->feedTruncated || !self->collectCurrentEntry;
    self->currentEntry = OpdsEntry{};
    self->currentText.clear();
    self->inTitle = self->inAuthor = self->inAuthorName = self->inId = false;
    return;
  }

  if (xmlNameMatches(name, "link")) {
    const char* href = findAttribute(atts, "href");
    if (href) {
      const char* rel = findAttribute(atts, "rel");
      const char* type = findAttribute(atts, "type");

      if (rel && strcmp(rel, "search") == 0) {
        if (strstr(href, "{searchTerms}") != nullptr) {
          // OPDS 1.1: search link carries the templated URL inline.
          assignBoundedOrReject(self->searchTemplate, href, MAX_SEARCH_TEMPLATE_CHARS, "search template");
        } else if (type && strstr(type, "opensearchdescription") != nullptr) {
          // OPDS 1.2 / OpenSearch 1.1: link points at a separate description
          // document that holds the real template. Fetched & parsed by the
          // caller only if no inline template was found.
          assignBoundedOrReject(self->searchDescriptionUrl, href, MAX_SEARCH_TEMPLATE_CHARS, "search description url");
        }
      } else if (rel && strcmp(rel, "next") == 0 && !self->inEntry) {
        assignBoundedOrReject(self->nextPageUrl, href, MAX_PAGE_URL_CHARS, "next page url");
      } else if (rel && strcmp(rel, "previous") == 0 && !self->inEntry) {
        assignBoundedOrReject(self->prevPageUrl, href, MAX_PAGE_URL_CHARS, "previous page url");
      }

      if (self->inEntry && self->collectCurrentEntry) {
        if (rel && type && strstr(rel, "opds-spec.org/acquisition") != nullptr &&
            strcmp(type, "application/epub+zip") == 0) {
          // Prefer plain EPUB links over derived formats when multiple
          // acquisition links are present for one entry.
          const bool isPlainEpub = strstr(href, ".epub") != nullptr || strstr(href, "/epub/") != nullptr;
          const bool alreadyHasPlainEpub = self->currentEntry.type == OpdsEntryType::BOOK &&
                                           (self->currentEntry.href.find(".epub") != std::string::npos ||
                                            self->currentEntry.href.find("/epub/") != std::string::npos);
          if (self->currentEntry.type != OpdsEntryType::BOOK || (isPlainEpub && !alreadyHasPlainEpub)) {
            self->currentEntry.type = OpdsEntryType::BOOK;
            // Reject rather than truncate: a book href feeds straight into
            // buildUrl() for the download request. A truncated href is a
            // different, broken URL, not a shortened valid one -- shipping it
            // just turns into a generic download failure with no way for the
            // user to learn why. Legitimate presigned/SAS download URLs
            // routinely exceed MAX_HREF_CHARS, so dropping the entry (title
            // stays set, but push requires href too) with a log is preferable
            // to downloading garbage.
            assignBoundedOrReject(self->currentEntry.href, href, MAX_HREF_CHARS, "entry href");
          }
        } else if (type && strstr(type, "application/atom+xml") != nullptr) {
          if (self->currentEntry.type != OpdsEntryType::BOOK) {
            self->currentEntry.type = OpdsEntryType::NAVIGATION;
            assignBoundedOrReject(self->currentEntry.href, href, MAX_HREF_CHARS, "entry href");
          }
        }
      }
    }
  }

  if (!self->inEntry || !self->collectCurrentEntry) return;

  if (xmlNameMatches(name, "title")) {
    self->inTitle = true;
    self->currentText.clear();
  } else if (xmlNameMatches(name, "author")) {
    self->inAuthor = true;
  } else if (self->inAuthor && (xmlNameMatches(name, "name"))) {
    self->inAuthorName = true;
    self->currentText.clear();
  } else if (xmlNameMatches(name, "id")) {
    self->inId = true;
    self->currentText.clear();
  }
}

void XMLCALL OpdsParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (xmlNameMatches(name, "entry")) {
    if (self->collectCurrentEntry && !self->currentEntry.title.empty() && !self->currentEntry.href.empty()) {
      self->entries.push_back(self->currentEntry);
    }
    self->inEntry = false;
    self->collectCurrentEntry = false;
  } else if (self->inEntry && self->collectCurrentEntry) {
    if (xmlNameMatches(name, "title")) {
      if (self->inTitle) self->currentEntry.title = self->currentText;
      self->inTitle = false;
    } else if (xmlNameMatches(name, "author")) {
      self->inAuthor = false;
    } else if (self->inAuthorName && (xmlNameMatches(name, "name"))) {
      self->currentEntry.author = self->currentText;
      self->inAuthorName = false;
    } else if (xmlNameMatches(name, "id")) {
      if (self->inId) self->currentEntry.id = self->currentText;
      self->inId = false;
    }
  }
}

void XMLCALL OpdsParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<OpdsParser*>(userData);
  if (!self->collectCurrentEntry) return;
  if (self->inTitle) {
    appendBounded(self->currentText, s, len, MAX_TITLE_CHARS);
  } else if (self->inAuthorName) {
    appendBounded(self->currentText, s, len, MAX_AUTHOR_CHARS);
  } else if (self->inId) {
    appendBounded(self->currentText, s, len, MAX_ID_CHARS);
  }
}

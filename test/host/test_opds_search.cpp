#include <string>

#include "doctest/doctest.h"
#include "lib/OpdsParser/OpdsParser.h"
#include "lib/OpdsParser/OpenSearchParser.h"

TEST_CASE("OpenSearchParser - Single Atom Url") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/search?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - Atom preferred over non-Atom") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"text/html\" template=\"http://example.com/html?q={searchTerms}\"/>\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/atom?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/atom?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - Non-Atom template fallback") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"text/html\" template=\"http://example.com/html?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/html?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - Namespace prefixed Url") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<os:OpenSearchDescription xmlns:os=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <os:Url type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "</os:OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/search?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - Url without searchTerms ignored") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/search?q=fixed\"/>\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/search?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - No Url or empty input") {
  std::string xmlNoUrl =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <ShortName>Test</ShortName>\n"
      "</OpenSearchDescription>";

  CHECK(OpenSearchParser::extractSearchTemplate(xmlNoUrl).empty());
  CHECK(OpenSearchParser::extractSearchTemplate("").empty());
}

TEST_CASE("OpenSearchParser - Malformed XML after valid Url") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "  <Malformed tag is not closed";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://example.com/search?q={searchTerms}");
}

TEST_CASE("OpenSearchParser - Malformed XML before Url") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Malformed tag is not closed\n"
      "  <Url type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result.empty());
}

TEST_CASE("OpenSearchParser - Template exceeding MAX_SEARCH_TEMPLATE_CHARS is truncated") {
  // OpenSearchParser applies its own bound (extractSearchTemplate's
  // strnlen(tmpl, MAX_SEARCH_TEMPLATE_CHARS)) independently of OpdsParser's
  // assignBounded/assignBoundedOrReject -- it's a separate implementation of
  // the same idea, and previously had no test exercising a template anywhere
  // near its 768-char limit.
  constexpr size_t kMaxSearchTemplateChars = 768;
  std::string longTemplate = "http://example.com/search?q={searchTerms}#" + std::string(900, 'x');
  REQUIRE(longTemplate.size() > kMaxSearchTemplateChars);

  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <Url type=\"application/atom+xml\" template=\"" +
      longTemplate +
      "\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result.size() == kMaxSearchTemplateChars);
  CHECK(result == longTemplate.substr(0, kMaxSearchTemplateChars));
}

TEST_CASE("OpenSearchParser - Booklore real-world shape") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<OpenSearchDescription xmlns=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <ShortName>Booklore Catalog Search</ShortName>\n"
      "  <Description>Search the Catalog</Description>\n"
      "  <InputEncoding>UTF-8</InputEncoding>\n"
      "  <OutputEncoding>UTF-8</OutputEncoding>\n"
      "  <Url type=\"text/html\" template=\"http://booklore.org/search?q={searchTerms}\"/>\n"
      "  <Url type=\"application/atom+xml\" "
      "template=\"http://booklore.org/search/atom?query={searchTerms}&amp;page={startPage?}\"/>\n"
      "  <Url type=\"application/x-suggestions+json\" template=\"http://booklore.org/suggest?q={searchTerms}\"/>\n"
      "</OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result == "http://booklore.org/search/atom?query={searchTerms}&page={startPage?}");
}

TEST_CASE("OpdsParser - Feed level inline search link") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <link rel=\"search\" href=\"http://example.com/search?q={searchTerms}\" type=\"application/atom+xml\"/>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getSearchTemplate() == "http://example.com/search?q={searchTerms}");
  CHECK(parser.getSearchDescriptionUrl().empty());
}

TEST_CASE("OpdsParser - Feed level description search link") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <link rel=\"search\" href=\"http://example.com/opensearch.xml\" "
      "type=\"application/opensearchdescription+xml\"/>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getSearchTemplate().empty());
  CHECK(parser.getSearchDescriptionUrl() == "http://example.com/opensearch.xml");
}

TEST_CASE("OpdsParser - Feed level no search link") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <link rel=\"self\" href=\"http://example.com/feed.xml\" type=\"application/atom+xml\"/>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getSearchTemplate().empty());
  CHECK(parser.getSearchDescriptionUrl().empty());
}

TEST_CASE("OpenSearchParser - Element suffix false match") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<os:OpenSearchDescription xmlns:os=\"http://a9.com/-/spec/opensearch/1.1/\">\n"
      "  <os:UrlType type=\"application/atom+xml\" template=\"http://example.com/search?q={searchTerms}\"/>\n"
      "</os:OpenSearchDescription>";

  std::string result = OpenSearchParser::extractSearchTemplate(xml);
  CHECK(result.empty());
}

TEST_CASE("OpdsParser - Element suffix false match") {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <linkType rel=\"search\" href=\"http://example.com/search?q={searchTerms}\" type=\"application/atom+xml\"/>\n"
      "  <os:linkType rel=\"search\" href=\"http://example.com/search?q={searchTerms}\" "
      "type=\"application/atom+xml\"/>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getSearchTemplate().empty());
  CHECK(parser.getSearchDescriptionUrl().empty());
}

TEST_CASE("OpdsParser - Feed entry bounds and truncation reporting") {
  // Construct a feed with 70 entries (exceeding MAX_ENTRIES = 62)
  std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\">\n";
  for (int i = 0; i < 70; ++i) {
    xml += "  <entry>\n";
    xml += "    <title>Book " + std::to_string(i) + "</title>\n";
    xml += "    <id>urn:book:" + std::to_string(i) + "</id>\n";
    xml += "    <link rel=\"http://opds-spec.org/acquisition\" href=\"http://example.com/books/" + std::to_string(i) +
           ".epub\" type=\"application/epub+zip\"/>\n";
    xml += "  </entry>\n";
  }
  xml += "</feed>\n";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getEntries().size() == 62);
  CHECK(parser.truncated());
  CHECK(parser.getEntries()[0].title == "Book 0");
  CHECK(parser.getEntries()[61].title == "Book 61");

  // Verify clear resets truncation
  parser.clear();
  CHECK_FALSE(parser.truncated());
  CHECK(parser.getEntries().empty());
}

TEST_CASE("OpdsParser - Feed within capacity not marked truncated") {
  std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\">\n";
  for (int i = 0; i < 10; ++i) {
    xml += "  <entry>\n";
    xml += "    <title>Book " + std::to_string(i) + "</title>\n";
    xml += "    <link rel=\"http://opds-spec.org/acquisition\" href=\"http://example.com/" + std::to_string(i) +
           ".epub\" type=\"application/epub+zip\"/>\n";
    xml += "  </entry>\n";
  }
  xml += "</feed>\n";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getEntries().size() == 10);
  CHECK_FALSE(parser.truncated());
}

TEST_CASE("OpdsParser - Field length bounds truncation") {
  // href is deliberately within MAX_HREF_CHARS here: this test is about the
  // non-URL text fields (title/author/id), which are still fine to truncate
  // -- a shortened title is still a valid, if shorter, title. A too-long
  // href is a different matter (see "Oversized href is rejected" below).
  std::string longTitle(250, 'T');
  std::string longAuthor(200, 'A');
  std::string longId(200, 'I');
  std::string validHref = "http://example.com/book.epub";

  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <entry>\n"
      "    <title>" +
      longTitle +
      "</title>\n"
      "    <author><name>" +
      longAuthor +
      "</name></author>\n"
      "    <id>" +
      longId +
      "</id>\n"
      "    <link rel=\"http://opds-spec.org/acquisition\" href=\"" +
      validHref +
      "\" type=\"application/epub+zip\"/>\n"
      "  </entry>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  REQUIRE(parser.getEntries().size() == 1);
  const auto& entry = parser.getEntries()[0];
  CHECK(entry.title.size() == 160);
  CHECK(entry.title == std::string(160, 'T'));
  CHECK(entry.author.size() == 120);
  CHECK(entry.author == std::string(120, 'A'));
  CHECK(entry.id.size() == 128);
  CHECK(entry.id == std::string(128, 'I'));
  CHECK(entry.href == validHref);
  CHECK_FALSE(parser.truncated());
}

TEST_CASE("OpdsParser - Oversized href is rejected rather than truncated") {
  // A presigned/SAS download URL over MAX_HREF_CHARS (768) is legitimate,
  // non-hostile content. Before this fix, assignBounded truncated it to 768
  // chars -- a different, broken URL -- and still pushed the entry
  // (entries.size() == 1, href.size() == 768). That truncated href then
  // reaches HttpDownloader as an opaque, generic failure. Rejecting the
  // whole entry (and logging why) is preferable to shipping a mangled URL.
  std::string longHref = "http://example.com/" + std::string(900, 'h') + ".epub";

  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <entry>\n"
      "    <title>Valid Title</title>\n"
      "    <link rel=\"http://opds-spec.org/acquisition\" href=\"" +
      longHref +
      "\" type=\"application/epub+zip\"/>\n"
      "  </entry>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getEntries().empty());
}

TEST_CASE("OpdsParser - Oversized pagination and search URLs rejected") {
  // Same "reject, don't corrupt" policy as entry href applies to every URL
  // field: a truncated next/prev/search link would silently build a broken
  // request URL downstream instead of the feed's real (longer) one. Before
  // this fix these were truncated to exactly 768 chars and returned as-is.
  std::string longNext = "http://example.com/next?" + std::string(900, 'n');
  std::string longPrev = "http://example.com/prev?" + std::string(900, 'p');
  std::string longSearch = "http://example.com/search?q={searchTerms}#" + std::string(900, 's');

  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <link rel=\"next\" href=\"" +
      longNext +
      "\" type=\"application/atom+xml\"/>\n"
      "  <link rel=\"previous\" href=\"" +
      longPrev +
      "\" type=\"application/atom+xml\"/>\n"
      "  <link rel=\"search\" href=\"" +
      longSearch +
      "\" type=\"application/atom+xml\"/>\n"
      "</feed>";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getNextPageUrl().empty());
  CHECK(parser.getPrevPageUrl().empty());
  CHECK(parser.getSearchTemplate().empty());
}

TEST_CASE("OpdsParser - Entry cap counts entries structurally") {
  // 70 <entry> elements, none with an href, so none are ever pushed to
  // `entries` (push requires both title and href). Before this fix,
  // collectCurrentEntry was gated on entries.size() < MAX_ENTRIES, which
  // never trips here since entries.size() stays 0 forever -- every one of
  // the 70 entries would still be fully tag-parsed with truncated() staying
  // false, an unbounded-CPU bypass of the entry cap.
  std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\">\n";
  for (int i = 0; i < 70; ++i) {
    xml += "  <entry><title>No Href " + std::to_string(i) + "</title></entry>\n";
  }
  xml += "</feed>\n";

  OpdsParser parser;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size());
  parser.flush();

  CHECK(parser.getEntries().empty());
  CHECK(parser.truncated());
}

TEST_CASE("OpdsParser - Feed body exceeding total size cap aborts parsing") {
  // A single href far larger than MAX_HREF_CHARS but with its closing quote
  // never reached before the 64KB total-body cap trips: expat cannot call
  // startElement("link", ...) until it has seen the complete, contiguous
  // attribute value, so this exercises the actual OOM path -- expat's own
  // buffer growing unboundedly across write() calls -- rather than the
  // separate (already-covered) "short-but-over-field-cap href" case.
  // Split into two write() calls to also confirm the byte count persists
  // across calls, matching how HTTP chunks arrive in production.
  std::string hugeHref = "http://example.com/" + std::string(100000, 'h') + ".epub";
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
      "  <entry>\n"
      "    <title>Huge Href Entry</title>\n"
      "    <link rel=\"http://opds-spec.org/acquisition\" href=\"" +
      hugeHref +
      "\" type=\"application/epub+zip\"/>\n"
      "  </entry>\n"
      "</feed>";

  OpdsParser parser;
  const size_t half = xml.size() / 2;
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()), half);
  parser.write(reinterpret_cast<const uint8_t*>(xml.data()) + half, xml.size() - half);
  parser.flush();

  CHECK(parser.truncated());
  CHECK(parser.getEntries().empty());
}

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

#pragma once

#include <string>

/**
 * Minimal parser for OpenSearch 1.1 description documents.
 *
 * OPDS 1.2 feeds advertise search by linking to a separate OpenSearch
 * description document (rel="search", type="application/opensearchdescription
 * +xml") instead of inlining a {searchTerms} template on the link itself.
 * This extracts the templated search URL from such a document.
 *
 * The input is the full description document as an in-memory string (these
 * documents are small and fixed-size, unlike OPDS feeds which are streamed).
 */
class OpenSearchParser {
 public:
  /**
   * Extract the best templated search URL from an OpenSearch description doc.
   *
   * Prefers a <Url> whose type contains "atom+xml" (an OPDS feed the device
   * can parse); falls back to any <Url> whose template contains
   * "{searchTerms}" if no Atom endpoint is advertised.
   *
   * @param xml The OpenSearch description document.
   * @return The templated URL (still containing "{searchTerms}"), or an empty
   *         string if none was found or the document was malformed.
   */
  static std::string extractSearchTemplate(const std::string& xml);
};

#pragma once

#include <expat.h>

#include <cstring>

// True when an element name matches a local name, with or without a namespace
// prefix: "link" and "atom:link" match "link"; "linkType"/"os:linkType" do not.
inline bool xmlNameMatches(const char* name, const char* local) {
  if (strcmp(name, local) == 0) return true;
  const char* colon = strrchr(name, ':');
  return colon != nullptr && strcmp(colon + 1, local) == 0;
}

// Safely tear down an expat parser: stop processing, clear callbacks, free, and null the pointer.
inline void destroyXmlParser(XML_Parser& parser) {
  if (!parser) return;
  XML_StopParser(parser, XML_FALSE);
  XML_SetElementHandler(parser, nullptr, nullptr);
  XML_SetCharacterDataHandler(parser, nullptr);
  XML_ParserFree(parser);
  parser = nullptr;
}

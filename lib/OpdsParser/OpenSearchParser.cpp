#include "OpenSearchParser.h"

#include <Logging.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <cstring>

namespace {

struct OpenSearchState {
  std::string best;        // chosen templated URL
  bool foundAtom = false;  // best came from an atom+xml <Url>
};

const char* findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  // Element is "Url" (default namespace) or "<prefix>:Url".
  if (strcmp(name, "Url") != 0 && strstr(name, ":Url") == nullptr) return;

  auto* state = static_cast<OpenSearchState*>(userData);
  if (state->foundAtom) return;  // an Atom endpoint already won

  const char* tmpl = findAttribute(atts, "template");
  if (!tmpl || strstr(tmpl, "{searchTerms}") == nullptr) return;

  const char* type = findAttribute(atts, "type");
  const bool isAtom = type && strstr(type, "atom+xml") != nullptr;

  // Atom always wins; otherwise keep the first templated URL as a fallback.
  if (isAtom) {
    state->best = tmpl;
    state->foundAtom = true;
  } else if (state->best.empty()) {
    state->best = tmpl;
  }
}

}  // namespace

std::string OpenSearchParser::extractSearchTemplate(const std::string& xml) {
  if (xml.empty()) return {};

  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_DBG("OPENSEARCH", "Couldn't allocate parser");
    return {};
  }

  OpenSearchState state;
  XML_SetUserData(parser, &state);
  XML_SetElementHandler(parser, startElement, nullptr);

  if (XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), XML_TRUE) == XML_STATUS_ERROR) {
    LOG_DBG("OPENSEARCH", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
            XML_ErrorString(XML_GetErrorCode(parser)));
    destroyXmlParser(parser);
    // A partial parse may still have captured a usable template.
    return std::move(state.best);
  }

  destroyXmlParser(parser);
  return std::move(state.best);
}

#include "OpdsShelfFetcher.h"

#include <OpdsParser.h>
#include <OpdsStream.h>

#include "network/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace OpdsShelfFetcher {
bool fetchRootBooks(const OpdsServer& server, std::vector<LibraryShelfEntry>& entries) {
  if (server.url.empty()) {
    return false;
  }

  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    const std::string url = UrlUtils::buildUrl(server.url, "");
    if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
      return false;
    }
  }
  if (!parser) {
    return false;
  }

  entries.clear();
  entries.reserve(6);
  for (const auto& entry : parser.getEntries()) {
    if (entry.type != OpdsEntryType::BOOK) {
      continue;
    }
    entries.push_back({entry.title, entry.author, entry.href});
    if (entries.size() >= 6) {
      break;
    }
  }
  return true;
}
}  // namespace OpdsShelfFetcher

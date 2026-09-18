#include "network/http/OpdsShelfFetcher.h"

#include <OpdsParser.h>
#include <OpdsStream.h>

#include "core/features/KoreaderOpdsBridge.h"
#include "network/http/HttpDownloader.h"
#include "util/UrlUtils.h"

namespace OpdsShelfFetcher {
http_fetch::Result fetchRootBooks(const OpdsServer& server, std::vector<LibraryShelfEntry>& entries) {
  if (server.url.empty()) {
    return http_fetch::Result{http_fetch::Reason::Unknown, 0};
  }

  OpdsParser parser;
  http_fetch::Result result;
  {
    OpdsParserStream stream{parser};
    const std::string url = UrlUtils::buildUrl(server.url, "");
    const auto creds = core::effectiveOpdsCredentials(server);
    result = HttpDownloader::fetchUrlResult(url, stream, creds.username, creds.password, false);
    if (!result.ok()) {
      return result;
    }
  }
  result.reason = http_fetch::classifyParse(parser.error(), parser.truncated(), parser.getEntries().size());
  if (result.reason != http_fetch::Reason::Ok) {
    return result;
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
  return result;
}
}  // namespace OpdsShelfFetcher

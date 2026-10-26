#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#include "doctest/doctest.h"

namespace {

std::string slurp(const char* path) {
  std::ifstream source(path);
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  return buffer.str();
}

size_t countNeedle(const std::string& text, const std::string& needle) {
  size_t count = 0;
  for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos; pos += needle.size()) {
    ++count;
  }
  return count;
}

}  // namespace

TEST_CASE("HttpDownloader HTTPS GET configs share one CRT-bundle helper") {
  const std::string text = slurp("src/network/http/HttpDownloader.cpp");

  CHECK(text.find("esp_crt_bundle_attach") != std::string::npos);
  CHECK(text.find("fillEspHttpGetConfig") != std::string::npos);
  CHECK(countNeedle(text, "fillEspHttpGetConfig(") == 4);
  CHECK(countNeedle(text, "config.crt_bundle_attach") == 1);
  CHECK(text.find("TimeSync::ensureTrustedClock") != std::string::npos);
  CHECK(text.find("admitHttps") != std::string::npos);
  CHECK(countNeedle(text, "admitHttps(") == 4);
  CHECK(text.find("max_authorization_retries = -1") != std::string::npos);

  const auto postPos = text.find("bool HttpDownloader::postJson");
  REQUIRE(postPos != std::string::npos);
  CHECK(text.find("setInsecure") > postPos);
}

TEST_CASE("OPDS HTTPS below the TLS floor does not reboot the device") {
  const std::string text = slurp("src/activities/browser/OpdsBookBrowserActivity.cpp");

  CHECK(text.find("silentRestart") == std::string::npos);
  CHECK(text.find("SilentRestart.h") != std::string::npos);
  CHECK(text.find("recoverHeapAfterWifi") != std::string::npos);
  CHECK(text.find("STR_FETCH_FEED_FAILED") != std::string::npos);
  CHECK(text.find("STR_OPDS_AUTH_REQUIRED") != std::string::npos);
  CHECK(text.find("STR_MEMORY_ERROR") != std::string::npos);
  CHECK(text.find("messageForFetchReason") != std::string::npos);
  CHECK(text.find("HttpDownloader::fetchUrlResult") != std::string::npos);
  CHECK(text.find("probeUrl") == std::string::npos);
  CHECK(text.find("BG_WIFI.stop(true)") != std::string::npos);
}

TEST_CASE("recoverHeapAfterWifi does not reboot") {
  const std::string text = slurp("src/main.cpp");
  const auto start = text.find("void recoverHeapAfterWifi");
  REQUIRE(start != std::string::npos);
  const auto end = text.find("\nnamespace {", start);
  REQUIRE(end != std::string::npos);
  const std::string body = text.substr(start, end - start);
  CHECK(body.find("silentRestart") == std::string::npos);
  CHECK(body.find("LeaveWifiForSession") != std::string::npos);
}

TEST_CASE("planner day-index header line last pixel is inclusive width-1") {
  const std::string indexText = slurp("src/activities/todo/DayIndexActivity.cpp");
  CHECK(indexText.find("drawLine(0, HEADER_HEIGHT, renderer.getScreenWidth(),") == std::string::npos);
  CHECK(indexText.find("lastCol") != std::string::npos);

  const std::string detailText = slurp("src/activities/todo/DayDetailActivity.cpp");
  CHECK(detailText.find("drawLine(0, HEADER_HEIGHT - 1, renderer.getScreenWidth(),") == std::string::npos);
  CHECK(detailText.find("getScreenWidth() - 1") != std::string::npos);

  constexpr int screenW = 480;
  constexpr int lastCol = screenW - 1;
  CHECK(screenW == 480);
  CHECK(lastCol == 479);
}

TEST_CASE("OPDS shelf fetch uses effective credentials and stays quiet on luxury skip") {
  const std::string text = slurp("src/network/http/OpdsShelfFetcher.cpp");
  CHECK(text.find("effectiveOpdsCredentials") != std::string::npos);
  CHECK(text.find("server.username, server.password") == std::string::npos);
  CHECK(text.find("fetchUrlResult") != std::string::npos);
  CHECK(text.find("classifyParse") != std::string::npos);
  CHECK(text.find("nullptr, false") == std::string::npos);
}

TEST_CASE("typed fetch failures do not log resource shortage as ERR") {
  const std::string text = slurp("src/network/http/HttpDownloader.cpp");
  CHECK(text.find("Heap too low for TLS") == std::string::npos);
  CHECK(text.find("http_fetch::classify") != std::string::npos);
  CHECK(text.find("logAsError") != std::string::npos);
}

TEST_CASE("Settings list rebuild leaves Always running") {
  const std::string text = slurp("src/activities/settings/SettingsActivity.cpp");
  const auto start = text.find("void SettingsActivity::rebuildSettingsLists()");
  REQUIRE(start != std::string::npos);
  const auto end = text.find("\nvoid ", start + 1);
  REQUIRE(end != std::string::npos);
  const std::string body = text.substr(start, end - start);
  CHECK(body.find("BG_WIFI.stop") == std::string::npos);
}

TEST_CASE("reader exit reclaims inflate occupancy") {
  const std::string text = slurp("src/activities/reader/EpubReaderActivity.cpp");
  CHECK(text.find("InflateReader::releaseSharedWindow") != std::string::npos);
}

TEST_CASE("cover thumbs bake inline on a first-page open, never in a background task") {
  // A bake task raced the reader's chapter inflate, so its policy demanded 64 KB
  // contiguous and no thumbnail was ever produced on device (2026-10-26).
  const std::string text = slurp("src/activities/reader/EpubReaderActivity.cpp");
  CHECK(text.find("xTaskCreate(coverThumbBakeTask") == std::string::npos);
  CHECK(text.find("pendingCoverThumbBake_ = openedAtFirstPage;") != std::string::npos);
  CHECK(text.find("CoverThumbHeapPolicy::canBakeInline") != std::string::npos);
}

TEST_CASE("reader onExit does not reclaim caches under the activity RenderLock") {
  const std::string text = slurp("src/activities/reader/EpubReaderActivity.cpp");
  const auto start = text.find("void EpubReaderActivity::onExit()");
  REQUIRE(start != std::string::npos);
  const auto end = text.find("\nvoid EpubReaderActivity::", start + 1);
  REQUIRE(end != std::string::npos);
  const std::string body = text.substr(start, end - start);
  CHECK(body.find("HeapReclaimRegistry::releaseAll") == std::string::npos);
  CHECK(body.find("releaseSharedWindow") != std::string::npos);
}

TEST_CASE("activity replace reclaims caches after dropping the RenderLock") {
  const std::string text = slurp("src/activities/ActivityManager.cpp");
  const auto start = text.find("} else if (pendingActivity) {");
  REQUIRE(start != std::string::npos);
  const auto enter = text.find("currentActivity->onEnter();", start);
  REQUIRE(enter != std::string::npos);
  const std::string body = text.substr(start, enter - start);
  const auto unlock = body.rfind("lock.unlock()");
  const auto reclaim = body.find("HeapReclaimRegistry::releaseAll()");
  REQUIRE(unlock != std::string::npos);
  REQUIRE(reclaim != std::string::npos);
  CHECK(unlock < reclaim);
}

TEST_CASE("serial walk can query activity and go home without BACK storms") {
  const std::string text = slurp("src/main.cpp");
  CHECK(text.find("cmd == \"ACTIVITY\"") != std::string::npos);
  CHECK(text.find("GOTO_OK") != std::string::npos);
  CHECK(text.find("currentActivityName") != std::string::npos);
  CHECK(text.find("GOTO_OK:Opds") != std::string::npos);
  CHECK(text.find("activityManager.goToBrowser()") != std::string::npos);
}

TEST_CASE("OPDS catalog GET is visible without debug logging") {
  const std::string text = slurp("src/activities/browser/OpdsBookBrowserActivity.cpp");
  CHECK(text.find("Catalog GET") != std::string::npos);
  CHECK(text.find("Catalog parsed:") != std::string::npos);
  CHECK(text.find("Catalog GET failed:") != std::string::npos);
}

TEST_CASE("library shelf latches authentication backoff") {
  const std::string text = slurp("src/network/background/BackgroundWifiService.cpp");
  CHECK(text.find("skipAfterAuthFailure") != std::string::npos);
  CHECK(text.find("shelfLastAuthFailureEpoch") != std::string::npos);
  CHECK(text.find("Library shelf refresh skipped: auth backoff") != std::string::npos);
  CHECK(text.find("Library shelf GET") != std::string::npos);
}

TEST_CASE("Lyra tab underline last pixel is inclusive width-1 clamped to the bar") {
  const std::string text = slurp("src/components/themes/lyra/LyraTheme.cpp");

  CHECK(text.find("currentX + textWidth + 2 * hPaddingInSelection,") == std::string::npos);
  CHECK(text.find("tabBoxWidth - 1") != std::string::npos);
  CHECK(text.find("lastCol") != std::string::npos);
  CHECK(text.find("underlineX2") != std::string::npos);

  constexpr int screenW = 480;
  constexpr int lastCol = screenW - 1;
  constexpr int currentX = 400;
  constexpr int tabBoxWidth = 80;
  const int oldX2 = currentX + tabBoxWidth;
  const int newX2 = std::min(currentX + tabBoxWidth - 1, lastCol);
  CHECK(oldX2 == 480);
  CHECK(newX2 == 479);
}

TEST_CASE("OPDS catalog load opens one TLS session; OpenSearch resolves on search") {
  // Device walk 2026-10-25: the catalog GET succeeded, then the OpenSearch
  // description GET in the same fetchFeed hit mbedtls_ssl_setup -0x7F00 (two
  // 16.7 KB I/O buffers vs a 32 KB largest block) and search silently vanished.
  const std::string text = slurp("src/activities/browser/OpdsBookBrowserActivity.cpp");
  const auto start = text.find("void OpdsBookBrowserActivity::fetchFeed(");
  REQUIRE(start != std::string::npos);
  const auto end = text.find("\nvoid OpdsBookBrowserActivity::", start + 1);
  REQUIRE(end != std::string::npos);
  const std::string body = text.substr(start, end - start);

  size_t requests = 0;
  for (size_t pos = body.find("HttpDownloader::fetchUrl"); pos != std::string::npos;
       pos = body.find("HttpDownloader::fetchUrl", pos + 1)) {
    ++requests;
  }
  CHECK(requests == 1);
  CHECK(body.find("searchDescriptionUrl =") != std::string::npos);

  const auto resolve = text.find("OpdsBookBrowserActivity::resolveSearchTemplate()");
  REQUIRE(resolve != std::string::npos);
  CHECK(text.find("OpenSearchParser::extractSearchTemplate", resolve) != std::string::npos);
  CHECK(text.find("resolveSearchTemplate();", text.find("void OpdsBookBrowserActivity::performSearch(")) !=
        std::string::npos);
}

TEST_CASE("reader open powers the STA radio down before the book loads") {
  // Device walk 2026-10-26: Always mode kept the radio associated (stop keepWifi=true)
  // through a book open; free heap hit 28 KB mid-index, under heapguard's 32 KB floor,
  // and every chapter after the cover failed with "Failed to load EPUB".
  const std::string text = slurp("src/activities/ActivityManager.cpp");
  const auto start = text.find("void ActivityManager::completeOpenReader()");
  REQUIRE(start != std::string::npos);
  const auto release = text.find("releaseRadioForReader()", start);
  const auto open = text.find("ReaderRegistry::open(", start);
  REQUIRE(release != std::string::npos);
  REQUIRE(open != std::string::npos);
  CHECK(release < open);
  // The On-Charge server's objects stop with keepWifi=true on purpose (2026-10-26
  // swarm review): its stop(false) would power the radio off with no idle-reclaim
  // wait. What must never happen is bgwifi keeping the radio through the load.
  CHECK(text.substr(start, open - start).find("BG_WIFI.stop(/*keepWifi=*/true)") == std::string::npos);

  const std::string reader = slurp("src/activities/reader/EpubReaderActivity.cpp");
  CHECK(reader.find("releaseRadioForReader()", reader.find("void EpubReaderActivity::onEnter()")) <
        reader.find("void EpubReaderActivity::onExit()"));
}

TEST_CASE("boot bakes missing covers before Wi-Fi and Home take the heap") {
  // Existing books never "arrive" again and rarely reopen on page one; boot is the
  // only moment outside a reader with the heap whole (2026-10-26).
  const std::string text = slurp("src/main.cpp");
  const auto catchUp = text.find("cover_bake::catchUpAtBoot(");
  REQUIRE(catchUp != std::string::npos);
  CHECK(catchUp > text.find("RECENT_BOOKS.loadFromFile();"));
  CHECK(catchUp < text.find("activityManager.goHome();", catchUp - 2000));
  // Smart mode has no boot connect at all (2026-10-26 redesign).
  CHECK(text.find("attemptBootAutoConnect") == std::string::npos);
}

TEST_CASE("a chapter that lost words is rebuilt after reclaim, never shown") {
  const std::string text = slurp("src/activities/reader/EpubReaderActivity.cpp");
  CHECK(text.find("truncatedIndexRetryPending_ = true;") != std::string::npos);
  CHECK(text.find("STR_CHAPTER_LOW_MEMORY") != std::string::npos);
  const std::string section = slurp("lib/Epub/Epub/Section.cpp");
  CHECK(section.find("section_cache::evaluateBuiltSection(ParsedText::heapTruncationTally())") != std::string::npos);
}

TEST_CASE("swarm review 2026-10-26: cover and truncation fixes stay in place") {
  const std::string section = slurp("lib/Epub/Epub/Section.cpp");
  const auto build = section.find("bool Section::createSectionFile(");
  REQUIRE(build != std::string::npos);
  // Tally reset precedes every early return, so a false return describes THIS build.
  CHECK(section.find("ParsedText::resetHeapTruncationTally();", build) < section.find("return false;", build));
  // A truncation refusal keeps resident-only CSS (partial parse, never cached).
  CHECK(section.find("if (cssParser && cssParser->hasCache()) {\n      cssParser->clear();") != std::string::npos);

  const std::string reader = slurp("src/activities/reader/EpubReaderActivity.cpp");
  CHECK(reader.find("truncationRetrySpineIndex_ != currentSpineIndex") != std::string::npos);
  CHECK(reader.find("if (css && css->hasCache()) {") != std::string::npos);

  const std::string dav = slurp("src/network/server/WebDAVHandler.cpp");
  const auto put = dav.find("void WebDAVHandler::handlePut(");
  CHECK(dav.find("CoverArrivalQueue::instance().push(", put) < dav.find("\nvoid WebDAVHandler::", put + 1));

  const std::string modules = slurp("src/core/features/FeatureModules.cpp");
  const auto fallback = modules.find("bool FeatureModules::tryGetDocumentCoverPath(");
  REQUIRE(fallback != std::string::npos);
  const std::string body = modules.substr(fallback, modules.find("\n}\n", fallback) - fallback);
  CHECK(body.find(".load(") == std::string::npos);
  CHECK(body.find("Xtc(") != std::string::npos);

  const std::string bake = slurp("src/features/epub/CoverThumbBake.cpp");
  // Crash guard: the failure marker is written before load/convert.
  CHECK(bake.find("markFailed(epub, true);") < bake.find("epub.load(/*buildIfMissing=*/true"));
  CHECK(bake.find("rememberPending(path);") != std::string::npos);
  CHECK(bake.find("homeCoverHeight") != std::string::npos);
}

TEST_CASE("Smart network sessions: radio only for book-close sessions and USB") {
  // User decision 2026-10-26: no resident radio. Closing a book requests a session;
  // the bgwifi server lingers 2 idle minutes and powers the radio down; USB power
  // is the On-Charge server's alone. Leaving File Transfer / Calibre keeps nothing.
  const std::string main = slurp("src/main.cpp");
  CHECK(main.find("requestSession(\"book closed\")") != std::string::npos);
  CHECK(main.find("attemptBootAutoConnect") == std::string::npos);

  const std::string service = slurp("src/network/background/BackgroundWifiService.cpp");
  CHECK(service.find("background_server::evaluateLinger(") != std::string::npos);
  CHECK(service.find("server->transferInProgress()") != std::string::npos);
  CHECK(service.find("(wifiOwned || lingerExpired)") != std::string::npos);

  const std::string coordinator = slurp("src/network/background/BackgroundWifiCoordinator.cpp");
  CHECK(coordinator.find("endSession(\"bgwifi stopped\")") != std::string::npos);
  CHECK(coordinator.find("wifiAutoConnectSkipCount") == std::string::npos);

  CHECK(slurp("src/activities/network/CrossPointWebServerActivity.cpp").find("Preserving WiFi connection") ==
        std::string::npos);
  CHECK(slurp("src/activities/network/CalibreConnectActivity.cpp").find("keepsBackgroundServerOnWifiWhileAwake") ==
        std::string::npos);
}

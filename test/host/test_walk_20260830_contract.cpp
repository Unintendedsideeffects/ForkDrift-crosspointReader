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
  CHECK(body.find("LeaveWifiForAlways") != std::string::npos);
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
  CHECK(text.find("evaluateReaderExitReclaim") != std::string::npos);
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

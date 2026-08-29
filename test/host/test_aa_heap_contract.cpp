#include <fstream>
#include <sstream>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("reader AA restores BW by repainting instead of allocating a second buffer") {
  std::ifstream source("src/activities/reader/ReaderUtils.h");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("storeBwBuffer") == std::string::npos);
  CHECK(text.find("restoreBwBuffer") == std::string::npos);
  CHECK(text.find("cleanupGrayscaleWithFrameBuffer") != std::string::npos);
}

TEST_CASE("epub page turn does not allocate an 8 KB grayscale strip scratch") {
  std::ifstream source("src/activities/reader/EpubReaderActivity.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("OOM: grayscale strip scratch") == std::string::npos);
  CHECK(text.find("STRIP_ROWS") == std::string::npos);
  CHECK(text.find("renderAntiAliased") != std::string::npos);
}

TEST_CASE("on-device Settings UI does not gate rebuild on 48 KB") {
  std::ifstream source("src/activities/settings/SettingsActivity.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("kMinHeapForSettingsRebuild") == std::string::npos);
  CHECK(text.find("48000") == std::string::npos);
  CHECK(text.find("getSettingsList(") == std::string::npos);
  CHECK(text.find("forEachSetting") != std::string::npos);
  CHECK(text.find("snapshotSetting") == std::string::npos);
  CHECK(text.find("BackgroundWebServer::getInstance().stop(true)") != std::string::npos);
  CHECK(text.find("kReader") != std::string::npos);
}

TEST_CASE("reader and controls overlays do not reboot to reclaim heap") {
  std::ifstream reader("src/activities/reader/ReaderOptionsActivity.cpp");
  REQUIRE(reader.good());
  std::ostringstream readerBuf;
  readerBuf << reader.rdbuf();
  const std::string readerText = readerBuf.str();
  CHECK(readerText.find("canBuildSettings") == std::string::npos);
  CHECK(readerText.find("memoryRecoveryRequested") == std::string::npos);
  CHECK(readerText.find("forEachSetting") != std::string::npos);

  std::ifstream controls("src/activities/reader/ControlsOptionsActivity.cpp");
  REQUIRE(controls.good());
  std::ostringstream controlsBuf;
  controlsBuf << controls.rdbuf();
  const std::string controlsText = controlsBuf.str();
  CHECK(controlsText.find("canBuildSettings") == std::string::npos);
  CHECK(controlsText.find("SMOKE_CTRL_TYPED_RECOVERY") == std::string::npos);
  CHECK(controlsText.find("forEachSetting") != std::string::npos);

  std::ifstream policy("src/activities/reader/ReaderOptionsMemoryPolicy.h");
  REQUIRE(policy.good());
  std::ostringstream policyBuf;
  policyBuf << policy.rdbuf();
  CHECK(policyBuf.str().find("canBuildSettings") == std::string::npos);
}

TEST_CASE("book open destroys the outgoing activity before ReaderRegistry::open") {
  std::ifstream source("src/activities/ActivityManager.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  const auto pendingPos = text.find("pendingAction = PendingAction::OpenReader");
  const auto completePos = text.find("void ActivityManager::completeOpenReader()");
  const auto reclaimPos = text.find("HeapReclaimRegistry::releaseAll()");
  const auto openPos = text.find("ReaderRegistry::open");
  REQUIRE(pendingPos != std::string::npos);
  REQUIRE(completePos != std::string::npos);
  REQUIRE(reclaimPos != std::string::npos);
  REQUIRE(openPos != std::string::npos);
  CHECK(pendingPos < completePos);
  CHECK(completePos < reclaimPos);
  CHECK(reclaimPos < openPos);
}

TEST_CASE("library shelf refresh runs before CrossPointWebServer construction") {
  std::ifstream source("src/network/background/BackgroundWifiService.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  const auto shelfPos = text.find("refreshLibraryShelf()");
  const auto serverPos = text.find("new (std::nothrow) CrossPointWebServer()");
  REQUIRE(shelfPos != std::string::npos);
  REQUIRE(serverPos != std::string::npos);
  CHECK(shelfPos < serverPos);
  CHECK(text.find("LIBRARY_SHELF_MIN_HEAP_BYTES") == std::string::npos);
  CHECK(text.find("84000") == std::string::npos);
  CHECK(text.find("library_shelf::evaluate") != std::string::npos);
  CHECK(text.find("PauseHttpThenRetry") != std::string::npos);
  CHECK(text.find("shelfRefreshAttempted = refreshLibraryShelf()") != std::string::npos);
  CHECK(text.find("kMinFreeBytes") != std::string::npos);
  CHECK(text.find("!stopRequested && !shelfRefreshAttempted") != std::string::npos);
}

TEST_CASE("indexing recovery does not silent-restart the device") {
  std::ifstream source("src/activities/reader/EpubReaderActivity.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("persistAndRestartForRecovery") == std::string::npos);
  CHECK(text.find("silentRestartToReader") == std::string::npos);
  CHECK(text.find("reclaimAfterIndexPressure") != std::string::npos);
  CHECK(text.find("decideAfterIndex") != std::string::npos);
  CHECK(text.find("CoverThumbHeapPolicy::canAttempt") != std::string::npos);
}

TEST_CASE("home cover thumbs consult CoverThumbHeapPolicy before EPUB inflate") {
  std::ifstream source("src/activities/home/HomeActivity.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();
  CHECK(text.find("CoverThumbHeapPolicy::canAttempt") != std::string::npos);
  CHECK(text.find("InflateReader::hasSharedWindow") != std::string::npos);
}

TEST_CASE("background UDP hello advertises a WS port only after ensureWs succeeds") {
  std::ifstream source("src/network/server/CrossPointWebServer.cpp");
  REQUIRE(source.good());
  std::ostringstream buffer;
  buffer << source.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("heapguard::canAllocate(WS_STARTUP_BYTES, 4096)") != std::string::npos);
  CHECK(text.find("if (wsReady && wsServer)") != std::string::npos);
}

TEST_CASE("firmware web route table is the only live registration path") {
  std::ifstream registry("src/core/registries/WebRouteRegistry.h");
  REQUIRE(registry.good());
  std::ostringstream buffer;
  buffer << registry.rdbuf();
  const std::string text = buffer.str();

  CHECK(text.find("#if defined(CROSSPOINT_HOST_BUILD) || defined(SIMULATOR)") != std::string::npos);
  CHECK(text.find("static void mountAll(WebServer* server)") != std::string::npos);

  std::ifstream server("src/network/server/CrossPointWebServer.cpp");
  REQUIRE(server.good());
  std::ostringstream serverBuf;
  serverBuf << server.rdbuf();
  const std::string serverText = serverBuf.str();
  CHECK(serverText.find("mountAll(") == std::string::npos);
  CHECK(serverText.find("WebRouteTableHandler") != std::string::npos);
}

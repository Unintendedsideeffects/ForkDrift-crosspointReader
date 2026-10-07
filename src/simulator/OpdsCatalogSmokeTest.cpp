#ifdef SIMULATOR
#include "OpdsCatalogSmokeTest.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>

#include "activities/ActivityManager.h"
#include "activities/browser/OpdsBookBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"

extern ActivityManager activityManager;
extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

struct OpdsCatalogSmokeTest {
  using State = OpdsBookBrowserActivity::BrowserState;
  using Request = OpdsBookBrowserActivity::DownloadRequest;
  static constexpr const char* ORIGINAL_TITLE = "Original requested book";

  static void require(bool ok, const char* message) {
    if (ok) return;
    LOG_ERR("SMOKE", "OPDS catalog: %s", message);
    std::_Exit(2);
  }

  static void render() {
    require(activityManager.requestUpdateAndWait() == RequestUpdateResult::Rendered, "render failed");
  }

  static void start(OpdsBookBrowserActivity& browser) {
    {
      RenderLock lock;
      require(browser.entries && browser.entryCount == 3, "fixture catalog missing");
      browser.selectorIndex = 2;
      browser.topIndex = 0;
      browser.descriptionLines.emplace_back("Allocated preview text that must be released before TLS");
    }
    browser.requestDownload(browser.entries[2]);
    RenderLock lock;
    require(browser.state == State::DOWNLOADING, "download did not start");
    require(!browser.entries && browser.entryCount == 0, "catalog retained during download");
    require(browser.descriptionLines.empty() && browser.descriptionLines.capacity() == 0, "preview retained");
    require(browser.statusMessage == "The Time Machine", "download title references freed catalog");
  }

  static void restored(OpdsBookBrowserActivity& browser) {
    render();
    RenderLock lock;
    require(browser.state == State::BROWSING && browser.entries && browser.entryCount == 3, "catalog not restored");
    require(browser.selectorIndex == 2 && browser.topIndex == 0, "browsing position lost");
    require(browser.currentPath == "/fiction" && browser.navigationHistory.size() == 1 &&
                browser.navigationHistory[0].empty() && !browser.searchTemplate.empty(),
            "navigation/search lost");
    require(!browser.catalogReleasedForDownload, "release state not reset");
  }

  static void outcomes(OpdsBookBrowserActivity& browser) {
    {
      RenderLock lock;
      browser.currentPath = "/fiction";
      browser.navigationHistory.emplace_back("");
    }
    browser.fetchFeed(browser.currentPath);
    start(browser);
    browser.finishDownload({}, HttpDownloader::OK, "/books/downloaded.epub");
    restored(browser);

    start(browser);
    OpdsBookBrowserActivity::onCancelEvent({}, &browser);
    browser.loop();
    restored(browser);

    for (const auto error : {HttpDownloader::HTTP_ERROR, HttpDownloader::INSUFFICIENT_SPACE}) {
      start(browser);
      browser.finishDownload({}, error, "");
      {
        RenderLock lock;
        require(browser.state == State::ERROR && !browser.entries && browser.catalogReleasedForDownload,
                "failed download lost error/retry state");
      }
      // Reconnecting after a failed transfer must restore the original view too.
      browser.onWifiSelectionComplete(true);
      restored(browser);
    }
  }

  static void collision(OpdsBookBrowserActivity& browser) {
    Request request{"https://opds.example/books/original.epub", ORIGINAL_TITLE, "/books/fallback.epub"};
    browser.downloadBook(request);
    browser.finishDownload(std::move(request), HttpDownloader::FILE_EXISTS, "/books/server-name.epub");
  }

  static void closeConfirmation(bool cancelled) {
    require(activityManager.isCurrentActivityNamed("Confirmation"), "overwrite confirmation missing");
    render();
    ActivityResult result;
    result.isCancelled = cancelled;
    activityManager.simulatorCurrentActivity()->setResult(std::move(result));
    activityManager.simulatorCurrentActivity()->finish();
  }

  static bool tick() {
    if (!std::getenv("CROSSINK_SIMULATOR_SMOKE_OPDS_CATALOG")) return false;
    static int phase = 0;
    static OpdsBookBrowserActivity* browser = nullptr;
    switch (phase++) {
      case 0: {
        OpdsServer server;
        server.name = "Catalog smoke";
        server.url = "https://opds.example";
        server.filenameFormat = OpdsFilenameFormat::SERVER_FILENAME;
        auto activity = std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInputManager, std::move(server));
        browser = activity.get();
        activityManager.replaceActivity(std::move(activity));
        break;
      }
      case 1:
        require(activityManager.isCurrentActivity(browser), "browser did not open");
        render();
        outcomes(*browser);
        collision(*browser);
        break;
      case 2:
        closeConfirmation(true);
        break;
      case 3:
        require(activityManager.isCurrentActivity(browser), "cancel did not return to catalog");
        restored(*browser);
        collision(*browser);
        break;
      case 4: {
        RenderLock lock;
        // The callback must use its owned request, even if this row changes.
        browser->entries[browser->selectorIndex].title = "Wrong replacement book";
        browser->entries[browser->selectorIndex].href = "/books/wrong.epub";
      }
        closeConfirmation(false);
        break;
      case 5:
        require(activityManager.isCurrentActivity(browser), "approval did not return to download");
        render();
        {
          RenderLock lock;
          require(browser->state == State::DOWNLOADING && !browser->entries, "approved download retained catalog");
          require(browser->statusMessage == ORIGINAL_TITLE, "overwrite approval changed selected book");
        }
        browser->finishDownload({}, HttpDownloader::ABORTED, "");
        restored(*browser);
        // A catalog that shrinks must clamp both indices before drawing rows.
        browser->downloadSelectorIndex = browser->downloadTopIndex = 50;
        browser->restoreCatalogAfterDownload();
        restored(*browser);
        // Existing locally named files must still prompt before releasing rows.
        std::snprintf(SETTINGS.opdsDownloadFolder, sizeof(SETTINGS.opdsDownloadFolder), "%s", "/books");
        browser->server.filenameFormat = OpdsFilenameFormat::TITLE_AUTHOR;
        require(Storage.writeFile("/books/The Time Machine - H. G. Wells.epub", "original"), "overwrite fixture");
        browser->requestDownload(browser->entries[2]);
        require(browser->entries != nullptr, "catalog released before initial overwrite approval");
        break;
      case 6:
        closeConfirmation(false);
        break;
      case 7:
        require(activityManager.isCurrentActivity(browser), "local overwrite approval did not start download");
        render();
        require(!browser->entries && browser->statusMessage == "The Time Machine", "local overwrite target lost");
        browser->finishDownload({}, HttpDownloader::ABORTED, "");
        restored(*browser);
        browser->server.filenameFormat = OpdsFilenameFormat::SERVER_FILENAME;
        start(*browser);
        browser->goHomeAfterCancel = true;
        browser->finishDownload({}, HttpDownloader::ABORTED, "");
        require(!browser->entries, "Home cancellation unnecessarily reloaded catalog");
        break;
      case 8:
        require(activityManager.isCurrentActivityNamed("Home"), "Home cancellation did not exit browser");
        render();
        LOG_INF("SMOKE", "Simulator smoke test passed: OPDS catalog release, success/cancel/retry, overwrite and Home");
        std::_Exit(0);
    }
    return true;
  }
};

bool tickOpdsCatalogSmokeTest() { return OpdsCatalogSmokeTest::tick(); }
#endif

#include "StatsUploadActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <LibraryBuilder.h>
#include <Memory.h>
#include <WiFi.h>
#include <Xtc.h>

#include <cstring>

#include "CrossPointState.h"
#include "HalClock.h"
#include "SdCardFontSystem.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/reader/BookStatsTracking.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/KOReaderSyncActivity.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/ReadingSyncUpload.h"
#include "network/WifiUtils.h"
#include "util/InputReleaseGuard.h"

namespace {
std::string documentIdFor(const std::string& bookPath) {
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(bookPath)
             : KOReaderDocumentId::calculate(bookPath);
}
}  // namespace

void StatsUploadActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseForNetwork(renderer);
  initialConfirm = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (!KOREADER_STORE.hasCredentials()) fail(tr(STR_SET_CREDENTIALS_FIRST));
  requestUpdate();
}

void StatsUploadActivity::closeTransfer() {
  index.close();
  folderOffsets.reset();
  folderBooks.reset();
  if (ownsWifi) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    ownsWifi = false;
  }
}

void StatsUploadActivity::onExit() {
  closeTransfer();
  Activity::onExit();
}

void StatsUploadActivity::leave() {
  // A single book was synced from its Library or File Browser menu: go back there,
  // keeping the browsing position, the same way EPUB Sync Book returns.
  if (scope == Scope::Book && returnTo.valid() &&
      (returnTo.origin == PendingOverlayOrigin::FileBrowser || returnTo.origin == PendingOverlayOrigin::Library)) {
    // The list restores its folder and selection from this on entry.
    const std::string folderPath = returnTo.fileBrowserPath;
    const bool fileBrowser = returnTo.origin == PendingOverlayOrigin::FileBrowser;
    APP_STATE.setPendingOverlayResume(std::move(returnTo));
    if (fileBrowser)
      activityManager.goToFileBrowser(folderPath);
    else
      activityManager.goToLibrary();
    return;
  }
  if (!returnBookPath.empty() && Storage.exists(returnBookPath.c_str())) {
    // The EPUB reader reopens the starting screen too; other readers resume the page.
    if (FsHelpers::hasEpubExtension(returnBookPath))
      activityManager.goToReaderAndRunMenuAction(std::move(returnBookPath), returnMenuAction);
    else
      activityManager.goToReader(std::move(returnBookPath), true);
    return;
  }
  finish();
}

const char* StatsUploadActivity::title() const {
  switch (scope) {
    case Scope::Library:
      return tr(STR_SYNC_ALL_BOOKS);
    case Scope::Folder:
      return tr(STR_FOLDER_SYNC);
    case Scope::Book:
      return tr(STR_SYNC_BOOK);
  }
  return tr(STR_SYNC);
}

void StatsUploadActivity::fail(const char* text) {
  LOG_ERR("StatsSync", "Upload stopped: %s", text);
  {
    RenderLock lock(*this);
    message = text;
    state = State::Failed;
  }
  closeTransfer();
  requestUpdate();
}

void StatsUploadActivity::failBook(std::string&& bookPath) {
  LOG_ERR("StatsSync", "Book sync failed: %s", bookPath.c_str());
  {
    RenderLock lock(*this);
    // EPUB loading may have run out of memory. Reuse the path's existing
    // allocation for the heading instead of allocating an error string.
    message = std::move(bookPath);
    message.erase(0, message.find_last_of('/') + 1);
    ++failed;
    state = State::BookFailed;
  }
  // Keep the book source and connection alive until Skip book or Back.
  requestUpdate();
}

void StatsUploadActivity::start() {
  if (scope == Scope::Folder) {
    folderBooks = makeUniqueNoThrow<FolderBookIterator>(path);
    if (!folderBooks) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
  } else if (scope == Scope::Library) {
    // Without an index only overall stats can be sent; the result asks for a refresh.
    libraryAvailable = index.open(library::libraryIndexPath());
    if (libraryAvailable && index.bookCount() > 0) {
      // Fixed 512-byte index accelerator: allocated only for this action,
      // avoiding a folder-table scan for every book without growing with the library.
      folderOffsets = makeUniqueNoThrow<uint32_t[]>(library::LibraryIndexFile::FOLDER_CHECKPOINT_COUNT);
      if (!folderOffsets) {
        fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
        return;
      }
      if (!index.buildFolderCheckpoints(folderOffsets.get(), folderStride)) {
        fail(tr(STR_STATS_UPLOAD_LIBRARY));
        return;
      }
    }
  }
  if (!hasActiveStationWifiConnection()) {
    ownsWifi = true;
    {
      RenderLock lock(*this);
      state = State::Wifi;
    }
    auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
    if (!wifi) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
    startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
      if (result.isCancelled) {
        leave();
        return;
      }
      if (!hasActiveStationWifiConnection()) {
        fail(tr(STR_CONNECTION_FAILED));
        return;
      }
      {
        RenderLock lock(*this);
        state = State::Uploading;
      }
      requestUpdate();
    });
  } else {
    {
      RenderLock lock(*this);
      state = State::Uploading;
    }
    requestUpdate();
  }
}

void StatsUploadActivity::finishUpload() {
  LOG_INF("StatsSync", "Finished: synced=%u skipped=%u failed=%u", static_cast<unsigned>(uploaded),
          static_cast<unsigned>(skipped), static_cast<unsigned>(failed));
  LOG_INF("StatsSync", "Extras: global=%d stats=%u statsFailed=%u clippings=%u clippingsFailed=%u",
          static_cast<int>(globalResult), static_cast<unsigned>(statsUploaded), static_cast<unsigned>(statsFailed),
          static_cast<unsigned>(clippingsUploaded), static_cast<unsigned>(clippingsFailed));
  closeTransfer();
  {
    RenderLock lock(*this);
    state = State::Done;
    message = libraryAvailable ? tr(STR_DONE) : tr(STR_STATS_UPLOAD_LIBRARY);
  }
  requestUpdate();
}

StatsUploadActivity::NextBook StatsUploadActivity::nextBook(std::string& bookPath) {
  switch (scope) {
    case Scope::Folder: {
      const auto next = folderBooks->next(bookPath);
      if (next == FolderBookIterator::Result::Done) return NextBook::Done;
      if (next == FolderBookIterator::Result::Error) return NextBook::Error;
      return next == FolderBookIterator::Result::Book ? NextBook::Book : NextBook::Skip;
    }
    case Scope::Library: {
      if (!libraryAvailable || ordinal >= index.bookCount()) return NextBook::Done;
      library::ClixRecord record;
      if (!index.readRecord(ordinal++, record) || !index.readPath(record, bookPath, folderOffsets.get(), folderStride))
        return NextBook::Error;
      // The index also lists TXT and Markdown books, which have nothing to sync.
      if (!FsHelpers::hasEpubExtension(bookPath) && !FsHelpers::hasXtcExtension(bookPath)) return NextBook::Skip;
      return Storage.exists(bookPath.c_str()) ? NextBook::Book : NextBook::Missing;
    }
    case Scope::Book:
      if (singleBookTaken) return NextBook::Done;
      singleBookTaken = true;
      bookPath = path;
      return NextBook::Book;
  }
  return NextBook::Done;
}

void StatsUploadActivity::uploadNext() {
  if (!globalAttempted) {
    // Batch ownership prevents books that skip/fail from starving overall stats,
    // and prevents retries of accepted global snapshots after a per-book failure.
    globalAttempted = true;
    if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      LOG_ERR("StatsSync", "Cannot render overall stats upload screen");
      {
        RenderLock lock(*this);
        globalResult = StatsUploadClient::Result::InvalidResponse;
      }
      requestUpdate();
      return;
    }
#ifndef SIMULATOR
    halClock.syncSystemTimeFromNTP();
#endif
    const auto result =
        KOREADER_STORE.getSyncStats() ? ReadingSyncUpload::globalStats() : StatsUploadClient::Result::Skipped;
    {
      RenderLock lock(*this);
      globalResult = result;
    }
    requestUpdate();
    return;
  }

  std::string bookPath;
  const auto next = nextBook(bookPath);
  if (next == NextBook::Done) {
    finishUpload();
    return;
  }
  if (next == NextBook::Error) {
    fail(scope == Scope::Library ? tr(STR_STATS_UPLOAD_LIBRARY) : tr(STR_FOLDER_SYNC_READ_FAILED));
    return;
  }
  if (next == NextBook::Skip) return;
  if (next == NextBook::Missing) {
    LOG_INF("StatsSync", "Skipping indexed book that no longer exists: %s", bookPath.c_str());
    {
      RenderLock lock(*this);
      ++skipped;
    }
    requestUpdate();
    return;
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    // XTC has no KOReader position: its saved stats are the whole sync.
    bool sent = false;
    if (!uploadBookExtras(bookPath, &sent)) {
      failBook(std::move(bookPath));
      return;
    }
    {
      RenderLock lock(*this);
      if (sent)
        ++uploaded;
      else
        ++skipped;
    }
    requestUpdate();
    return;
  }
  syncEpub(std::move(bookPath));
}

void StatsUploadActivity::recordExtras(const bool statsOk, const bool clippingsOk, const bool statsError,
                                       const bool clippingsError) {
  RenderLock lock(*this);
  statsUploaded += statsOk;
  clippingsUploaded += clippingsOk;
  statsFailed += statsError;
  clippingsFailed += clippingsError;
}

bool StatsUploadActivity::uploadBookExtras(const std::string& bookPath, bool* sentAny) {
  if (sentAny) *sentAny = false;
  if (!KOREADER_STORE.getSyncStats() && !KOREADER_STORE.getSyncClippings()) return true;
  const auto document = documentIdFor(bookPath);
  if (document.empty()) return false;
  const auto result = ReadingSyncUpload::extras(bookPath, document);
  if (sentAny)
    *sentAny = result.stats == StatsUploadClient::Result::Ok || result.clippings == StatsUploadClient::Result::Ok;
  recordExtras(result.stats == StatsUploadClient::Result::Ok, result.clippings == StatsUploadClient::Result::Ok,
               StatsUploadClient::failed(result.stats), StatsUploadClient::failed(result.clippings));
  return result.success();
}

void StatsUploadActivity::syncEpub(std::string&& bookPath) {
  {
    // Rebuild missing metadata (for example after moving a book into Read),
    // but release it before the child makes TLS requests.
    auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (!epub) {
      fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
      return;
    }
    EpubReaderUtils::Progress saved;
    if (!EpubReaderUtils::loadProgress(*epub, saved) || !saved.hasPageCount || saved.pageCount < 1 ||
        saved.pageNumber >= saved.pageCount) {
      LOG_INF("StatsSync", "Skipping EPUB progress without usable saved position: %s", bookPath.c_str());
      epub.reset();
      if (!uploadBookExtras(bookPath)) {
        failBook(std::move(bookPath));
        return;
      }
      {
        RenderLock lock(*this);
        ++skipped;
      }
      requestUpdate();
      return;
    }
    // Refresh the panel only for books that will reach the network, not for
    // every unread EPUB in a large Library.
    if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      fail(tr(STR_SYNC_FAILED_MSG));
      return;
    }
    if (!epub->load(true, true, Epub::XLocationLoadMode::Skip, true) || saved.spineIndex < 0 ||
        saved.spineIndex >= epub->getSpineItemsCount()) {
      epub.reset();
      uploadBookExtras(bookPath);
      failBook(std::move(bookPath));
      return;
    }
  }
  auto sync = makeUniqueNoThrow<KOReaderSyncActivity>(renderer, mappedInput, bookPath, KOREADER_STORE.getMatchMethod(),
                                                      SETTINGS.orientation, true, false);
  if (!sync) {
    fail(tr(STR_KOREADER_SYNC_LOW_MEMORY));
    return;
  }
  {
    RenderLock lock(*this);
    state = State::SyncingBook;
  }
  startActivityForResult(std::move(sync), [this](const ActivityResult& result) {
    // The book screen's Exit stops the whole bulk sync; Skip book continues it.
    if (result.isCancelled) {
      LOG_INF("StatsSync", "Exited: synced=%u skipped=%u failed=%u", static_cast<unsigned>(uploaded),
              static_cast<unsigned>(skipped), static_cast<unsigned>(failed));
      leave();
      return;
    }
    const auto* synced = std::get_if<ProgressSyncResult>(&result.data);
    if (!synced) {
      fail(tr(STR_SYNC_FAILED_MSG));
      return;
    }
    recordExtras(synced->statsUploaded, synced->clippingsUploaded, synced->statsFailed, synced->clippingsFailed);
    {
      RenderLock lock(*this);
      if (synced->progressSucceeded) ++uploaded;
      if (synced->skipped)
        ++skipped;
      else if (!synced->success)
        ++failed;
      state = State::Uploading;
    }
    requestUpdate();
  });
}

void StatsUploadActivity::loop() {
  if (InputReleaseGuard::consumeInitialRelease(mappedInput, MappedInputManager::Button::Confirm, initialConfirm))
    return;
  // A single book, or a bulk sync the caller already confirmed: nothing to ask.
  if (autoStart && state == State::Ready) {
    start();
    return;
  }
  // Each upload step blocks and buttons are polled, so a held Exit also counts
  // while uploading; a quick press during a request would otherwise be missed.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      (!singleBook() && state == State::Uploading && mappedInput.isPressed(MappedInputManager::Button::Back)) ||
      TouchHeaderBackButton::wasTapped(mappedInput, renderer)) {
    mappedInput.suppressNextBackRelease();
    leave();
    return;
  }
  if (state == State::Ready || state == State::BookFailed || state == State::Done) {
    int x = 0, y = 0;
    bool actionTapped = false;
    if (mappedInput.wasScreenTapped(x, y)) {
      const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      const auto layout =
          TouchActionButtons::vertical(Rect{area.x + 20, area.y + area.height - 80, area.width - 40, 56}, 1);
      actionTapped = TouchActionButtons::indexAt(layout, x, y) == 0;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || actionTapped) {
      if (state == State::Done) {
        leave();
        return;
      } else if (state == State::Ready) {
        start();
      } else {
        {
          RenderLock lock(*this);
          state = State::Uploading;
        }
        requestUpdate();
      }
    }
  } else if (state == State::Uploading) {
    // One book per loop permits cancellation between books. No background
    // task or persistent upload queue can start networking outside this action.
    uploadNext();
  }
}

void StatsUploadActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  const char* heading = state == State::BookFailed ? message.c_str() : title();
  if (mappedInput.hasTouchHardware())
    TouchHeaderBackButton::draw(renderer, header, heading, false);
  else
    GUI.drawHeader(renderer, header, heading);
  const auto area = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int y = area.y + area.height / 3;
  const bool asking = state == State::Ready && !autoStart;
  const char* text = asking ? (scope == Scope::Library ? tr(STR_SYNC_ALL_CONFIRM) : tr(STR_FOLDER_SYNC_CONFIRM))
                     : state == State::Ready || state == State::Uploading ? tr(STR_LOADING)
                     : state == State::BookFailed                         ? tr(STR_SYNC_FAILED_MSG)
                                                                          : message.c_str();
  int detailY =
      y + UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, y, text, 3, true, EpdFontFamily::REGULAR, 4) +
      24;
  if (state == State::Ready && autoStart) {
    // About to connect; there are no results to show yet.
  } else if (asking) {
    if (scope != Scope::Library) {
      detailY += UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, detailY, path.c_str(), 3, true,
                                                  EpdFontFamily::REGULAR, 4) +
                 8;
    }
    const auto url = KOREADER_STORE.getBaseUrl();
    UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, detailY, url.c_str(), 3, true,
                                     EpdFontFamily::REGULAR, 4);
  } else {
    char counts[160];
    snprintf(counts, sizeof(counts), tr(STR_FOLDER_SYNC_COUNTS), static_cast<unsigned>(uploaded),
             static_cast<unsigned>(skipped), static_cast<unsigned>(failed));
    detailY += UITheme::drawCenteredWrappedText(renderer, area, UI_10_FONT_ID, detailY, counts, 3, true,
                                                EpdFontFamily::REGULAR, 4) +
               24;
    char statsCount[48];
    char clippingsCount[48];
    const char* statsText =
        ReadingSyncUpload::countLabel(statsCount, sizeof(statsCount), statsUploaded, statsFailed, false);
    const char* clippingsText =
        ReadingSyncUpload::countLabel(clippingsCount, sizeof(clippingsCount), clippingsUploaded, clippingsFailed, true);
    int rowY = detailY;
    rowY += UITheme::drawCenteredStatusRow(renderer, area, UI_10_FONT_ID, rowY, tr(STR_ALL_TIME_STATS),
                                           ReadingSyncUpload::statusLabel(globalResult, false)) +
            8;
    rowY += UITheme::drawCenteredStatusRow(renderer, area, UI_10_FONT_ID, rowY, tr(STR_READING_STATS), statsText) + 8;
    UITheme::drawCenteredStatusRow(renderer, area, UI_10_FONT_ID, rowY, tr(STR_CLIPPINGS), clippingsText);
  }
  const char* action = asking                       ? tr(STR_SYNC)
                       : state == State::BookFailed ? tr(STR_SKIP_BOOK)
                       : state == State::Done       ? tr(STR_BACK)
                                                    : "";
  if (action[0] && mappedInput.hasTouchHardware()) {
    const auto layout =
        TouchActionButtons::vertical(Rect{area.x + 20, area.y + area.height - 80, area.width - 40, 56}, 1);
    const char* labels[] = {action};
    TouchActionButtons::draw(renderer, layout, labels, 0);
  }
  // Mid-run, Back leaves the whole sync rather than one book or screen.
  const bool running = !singleBook() && (state == State::Uploading || state == State::BookFailed);
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(running ? tr(STR_EXIT) : tr(STR_BACK)), action, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

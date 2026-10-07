#pragma once

#include <LibraryIndexFile.h>

#include <memory>
#include <string>

#include "PendingOverlayResume.h"
#include "activities/Activity.h"
#include "network/StatsUploadClient.h"
#include "util/FolderBookIterator.h"

// Bulk "Sync" for several books: progress, reading stats, and clippings for each
// book, plus overall stats once. Only entered from an explicit user action; no
// reader/background hooks.
class StatsUploadActivity final : public Activity {
 public:
  enum class Scope : uint8_t {
    Library,    // Every book in the Library index ("Sync All Books").
    Folder,     // A File Browser folder and its subfolders ("Sync Folder").
    Book,       // One book without a KOReader position (XTC): stats only.
    Clippings,  // One EPUB's clippings, asked for from its clippings list.
  };
  // Sync All Books.
  StatsUploadActivity(GfxRenderer& renderer, MappedInputManager& input)
      : Activity("StatsUpload", renderer, input), scope(Scope::Library) {}
  // `confirmed`: the caller already asked, so start without the confirmation screen.
  StatsUploadActivity(GfxRenderer& renderer, MappedInputManager& input, std::string path, Scope scope = Scope::Folder,
                      bool confirmed = false)
      : Activity("StatsUpload", renderer, input),
        scope(scope),
        autoStart(confirmed || scope == Scope::Book || scope == Scope::Clippings),
        path(std::move(path)) {}
  // Book scope: the Library/File Browser view to return to. Kept here, not in
  // global state, so leaving another way (Home, sleep) leaves nothing stale.
  void setReturnTo(PendingOverlayResume resume) { returnTo = std::move(resume); }
  // Started from a screen over a reader: reopen that book afterwards, the way a
  // reader-started sync resumes after its silent restart. The EPUB reader then
  // runs `readerMenuAction` (an EpubReaderMenuAction) to reopen that screen.
  void setReturnToBook(std::string bookPath, uint8_t readerMenuAction) {
    returnBookPath = std::move(bookPath);
    returnMenuAction = readerMenuAction;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Uploading; }

 private:
  enum class State { Ready, Wifi, Uploading, SyncingBook, BookFailed, Done, Failed };
  enum class NextBook { Book, Skip, Missing, Done, Error };  // Missing: indexed but deleted.
  State state = State::Ready;
  const Scope scope;
  const bool autoStart = false;  // A single book or a pre-confirmed bulk sync skips Ready.
  std::string path;              // Folder root or single book; empty for Library.
  PendingOverlayResume returnTo;
  std::string returnBookPath;
  uint8_t returnMenuAction = 0;
  library::LibraryIndexFile index;
  std::unique_ptr<uint32_t[]> folderOffsets;
  uint16_t folderStride = 0;
  uint16_t ordinal = 0;
  uint32_t uploaded = 0;
  uint32_t skipped = 0;
  uint32_t failed = 0;
  uint32_t statsUploaded = 0;
  uint32_t clippingsUploaded = 0;
  uint32_t statsFailed = 0;
  uint32_t clippingsFailed = 0;
  StatsUploadClient::Result globalResult = StatsUploadClient::Result::Skipped;
  StatsUploadClient::Result clippingsResult = StatsUploadClient::Result::Skipped;  // Clippings scope only.
  std::unique_ptr<FolderBookIterator> folderBooks;
  bool globalAttempted = false;
  bool singleBookTaken = false;
  bool libraryAvailable = true;
  bool ownsWifi = false;
  bool initialConfirm = false;
  std::string message;
  NextBook nextBook(std::string& bookPath);
  const char* title() const;
  bool singleBook() const { return scope == Scope::Book || scope == Scope::Clippings; }
  void start();
  void uploadNext();
  void syncEpub(std::string&& bookPath);
  void uploadClippings();
  bool uploadBookExtras(const std::string& bookPath, bool* sentAny = nullptr);
  void leave();
  void recordExtras(bool statsOk, bool clippingsOk, bool statsError, bool clippingsError);
  void fail(const char* text);
  void failBook(std::string&& bookPath);
  void finishUpload();
  void closeTransfer();
};

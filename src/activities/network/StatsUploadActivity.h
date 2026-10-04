#pragma once

#include <LibraryIndexFile.h>

#include <memory>

#include "activities/Activity.h"
#include "network/StatsUploadClient.h"
#include "util/FolderBookIterator.h"

// Only entered from an explicit settings action; no reader/background hooks.
class StatsUploadActivity final : public Activity {
 public:
  StatsUploadActivity(GfxRenderer& renderer, MappedInputManager& input) : Activity("StatsUpload", renderer, input) {}
  StatsUploadActivity(GfxRenderer& renderer, MappedInputManager& input, std::string folder)
      : Activity("StatsUpload", renderer, input), folder(std::move(folder)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Uploading; }

 private:
  enum class State { Ready, Wifi, Uploading, SyncingBook, BookFailed, Done, Failed };
  State state = State::Ready;
  library::LibraryIndexFile index;
  std::unique_ptr<StatsUploadClient> client;
  std::unique_ptr<char[]> payload;
  std::unique_ptr<uint32_t[]> folderOffsets;
  uint16_t folderStride = 0;
  char deviceId[32]{};
  uint16_t ordinal = 0;
  uint32_t uploaded = 0;
  uint32_t skipped = 0;
  uint32_t failed = 0;
  uint32_t statsUploaded = 0;
  uint32_t clippingsUploaded = 0;
  uint32_t statsFailed = 0;
  uint32_t clippingsFailed = 0;
  StatsUploadClient::Result globalResult = StatsUploadClient::Result::Skipped;
  std::string folder;
  std::unique_ptr<FolderBookIterator> folderBooks;
  bool globalAttempted = false;
  bool globalUploaded = false;
  bool libraryAvailable = false;
  bool ownsWifi = false;
  bool initialConfirm = false;
  std::string message;
  void start();
  void uploadNext();
  void uploadFolderBook();
  bool uploadFolderExtras(const std::string& path);
  void recordExtras(bool statsOk, bool clippingsOk, bool statsError, bool clippingsError);
  bool send(const char* endpoint, bool book);
  void fail(const char* text);
  void failBook(std::string&& path);
  void finishUpload();
  void closeTransfer();
};

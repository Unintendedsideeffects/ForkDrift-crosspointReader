#pragma once
#include <Epub.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"
#include "activities/Activity.h"

class KOReaderSyncActivity final : public Activity {
 public:
  explicit KOReaderSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& epubPath,
                                const CrossPointPosition& localPosition, SavedProgressPosition localKoPos,
                                std::string localChapterName)
      : Activity("KOReaderSync", renderer, mappedInput),
        epubPath(epubPath),
        localChapterName(std::move(localChapterName)),
        localPosition(localPosition),
        remoteProgress{},
        remotePosition{},
        localProgress(std::move(localKoPos)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CONNECTING || state == SYNCING; }

 private:
  enum State {
    WIFI_SELECTION,
    CONNECTING,
    SYNCING,
    SHOWING_RESULT,
    UPLOADING,
    UPLOAD_COMPLETE,
    NO_REMOTE_PROGRESS,
    SYNC_FAILED,
    NO_CREDENTIALS
  };

  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::string localChapterName;
  CrossPointPosition localPosition;

  State state = WIFI_SELECTION;
  std::string statusMessage;
  std::string documentHash;

  bool hasRemoteProgress = false;
  KOReaderProgress remoteProgress;
  CrossPointPosition remotePosition;

  SavedProgressPosition localProgress;

  int selectedOption = 0;

  TaskHandle_t syncTaskHandle = nullptr;
  std::atomic<bool> syncTaskExited{false};

  bool wifiActivated = false;

  bool lockInitialConfirmRelease = false;

  void onWifiSelectionComplete(bool success);
  void performSync();
  void performUpload();
  bool consumeInitialConfirmRelease();
  void ensureEpubLoaded();
  void saveProgressAndReturn(int spineIndex, int page);
  void returnToReader();
};

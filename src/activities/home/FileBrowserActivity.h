#pragma once

#include <string>
#include <utility>
#include <vector>

#include <FeatureFlags.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  enum class Mode { Books, PickFirmware };

 private:
  ButtonNavigator buttonNavigator;
  bool lockNextConfirmRelease = false;

  Mode mode = Mode::Books;
  std::string basepath = "/";
  std::vector<std::string> files;
  size_t selectorIndex = 0;
  bool lockLongPressBack = false;
  bool longPressBackHandled = false;
  bool longPressConfirmHandled = false;
#if ENABLE_READING_STATS
  bool pendingCompletedFeedback = false;
  bool completedFeedbackIsFinished = false;
  unsigned long completedFeedbackShowTime = 0UL;
#endif

  void loadFiles();
  void toggleHiddenFiles();
  void confirmDeleteEntry(const std::string& entry);
  void clearFileMetadata(const std::string& fullPath);
  bool clearBookCache(const std::string& fullPath);
#if ENABLE_READING_STATS
  bool isEpubCompleted(const std::string& fullPath) const;
  void toggleEpubCompleted(const std::string& fullPath, const std::string& entry);
#endif
  void pinSleepFavorite(const std::string& fullPath);
  void unpinSleepFavorite();
  bool isPinnedSleepFavorite(const std::string& fullPath) const;
  void showFileActionMenu(const std::string& entry, bool ignoreInitialConfirmRelease = false);
  void onSelectBook(const std::string& fullPath);
  void onGoHome();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};

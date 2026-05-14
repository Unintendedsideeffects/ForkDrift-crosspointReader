#include "CrossPointState.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>
#include <Serialization.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <utility>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 4;
constexpr char STATE_FILE_BIN[] = "/.crosspoint/state.bin";
constexpr char STATE_FILE_JSON[] = "/.crosspoint/state.json";
constexpr char STATE_FILE_BAK[] = "/.crosspoint/state.bin.bak";

SemaphoreHandle_t pendingStateMutex() {
  static StaticSemaphore_t mutexStorage;
  static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
  return mutex;
}

class PendingStateLock {
 public:
  PendingStateLock() : mutex_(pendingStateMutex()) {
    if (mutex_ != nullptr) {
      const TaskHandle_t prevHolder = xSemaphoreGetMutexHolder(mutex_);
      if (prevHolder != nullptr) {
        LOG_DBG("PSL", "take by '%s' (prev holder='%s')", pcTaskGetName(nullptr), pcTaskGetName(prevHolder));
      }
      held_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
    }
  }

  ~PendingStateLock() {
    if (mutex_ == nullptr || !held_) {
      return;
    }
    const TaskHandle_t self = xTaskGetCurrentTaskHandle();
    if (xSemaphoreGetMutexHolder(mutex_) != self) {
      const TaskHandle_t holder = xSemaphoreGetMutexHolder(mutex_);
      LOG_ERR("PSL", "skip give (not holder): self='%s' holder='%s'", pcTaskGetName(self),
              holder ? pcTaskGetName(holder) : "<none>");
      return;
    }
    xSemaphoreGive(mutex_);
  }

  PendingStateLock(const PendingStateLock&) = delete;
  PendingStateLock& operator=(const PendingStateLock&) = delete;

 private:
  SemaphoreHandle_t mutex_{};
  bool held_{false};
};
}  // namespace

CrossPointState CrossPointState::instance;

// Exposed for BackgroundWifiService to check whether a task it's about to
// vTaskDelete still holds this mutex (which would brick later Gives).
TaskHandle_t debugPendingStateMutexHolder() {
  SemaphoreHandle_t m = pendingStateMutex();
  return m ? xSemaphoreGetMutexHolder(m) : nullptr;
}

bool CrossPointState::isRecentSleep(uint16_t idx, uint8_t checkCount) const {
  const uint8_t effectiveCount = std::min(checkCount, recentSleepFill);
  for (uint8_t i = 0; i < effectiveCount; i++) {
    const uint8_t slot = (recentSleepPos + SLEEP_RECENT_COUNT - 1 - i) % SLEEP_RECENT_COUNT;
    if (recentSleepImages[slot] == idx) return true;
  }
  return false;
}

void CrossPointState::pushRecentSleep(uint16_t idx) {
  recentSleepImages[recentSleepPos] = idx;
  recentSleepPos = (recentSleepPos + 1) % SLEEP_RECENT_COUNT;
  if (recentSleepFill < SLEEP_RECENT_COUNT) recentSleepFill++;
}

bool CrossPointState::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveState(*this, STATE_FILE_JSON);
}

bool CrossPointState::loadFromFile() {
  // Try JSON first
  if (Storage.exists(STATE_FILE_JSON)) {
    String json = Storage.readFile(STATE_FILE_JSON);
    if (!json.isEmpty()) {
      return JsonSettingsIO::loadState(*this, json.c_str());
    }
  }

  // Fall back to binary migration
  if (Storage.exists(STATE_FILE_BIN)) {
    if (loadFromBinaryFile()) {
      if (saveToFile()) {
        Storage.rename(STATE_FILE_BIN, STATE_FILE_BAK);
        LOG_DBG("CPS", "Migrated state.bin to state.json");
        return true;
      } else {
        LOG_ERR("CPS", "Failed to save state during migration");
      }
    }
  }

  return false;
}

void CrossPointState::setPendingOpenPath(std::string path) {
  PendingStateLock lock;
  pendingOpenPath = std::move(path);
}

std::string CrossPointState::takePendingOpenPath() {
  PendingStateLock lock;
  std::string path = std::move(pendingOpenPath);
  pendingOpenPath.clear();
  if (!path.empty()) {
    pendingPageTurn = 0;
  }
  return path;
}

void CrossPointState::setPendingPageTurn(const int8_t pageTurn) {
  PendingStateLock lock;
  pendingPageTurn = pageTurn;
}

int8_t CrossPointState::takePendingPageTurn() {
  PendingStateLock lock;
  const int8_t pageTurn = pendingPageTurn;
  pendingPageTurn = 0;
  return pageTurn;
}

bool CrossPointState::loadFromBinaryFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE_BIN, inputFile)) {
    return false;
  }

  uint8_t version;
  if (!serialization::readPod(inputFile, version)) {
    LOG_ERR("CPS", "Failed to read version");
    inputFile.close();
    return false;
  }
  if (version > STATE_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  if (!serialization::readString(inputFile, openEpubPath)) {
    LOG_ERR("CPS", "Failed to read epub path");
    inputFile.close();
    return false;
  }

  if (version >= 2) {
    uint8_t legacyLastSleep = UINT8_MAX;
    serialization::readPod(inputFile, legacyLastSleep);
    if (legacyLastSleep != UINT8_MAX) {
      pushRecentSleep(static_cast<uint16_t>(legacyLastSleep));
    }
  }

  if (version >= 3) {
    if (!serialization::readPod(inputFile, readerActivityLoadCount)) {
      LOG_ERR("CPS", "Failed to read reader activity counter");
      inputFile.close();
      return false;
    }
  }

  if (version >= 4) {
    if (!serialization::readPod(inputFile, lastSleepFromReader)) {
      LOG_ERR("CPS", "Failed to read sleep source flag");
      inputFile.close();
      return false;
    }
  } else {
    lastSleepFromReader = false;
  }

  return true;
}

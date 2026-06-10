#include "SpiBusMutex.h"

#include <Logging.h>
#include <freertos/task.h>

namespace {
StaticSemaphore_t spiMutexBuffer;

SemaphoreHandle_t createMutex() { return xSemaphoreCreateRecursiveMutexStatic(&spiMutexBuffer); }
}  // namespace

SemaphoreHandle_t SpiBusMutex::get() {
  static SemaphoreHandle_t spiMutex = createMutex();
  return spiMutex;
}

void SpiBusMutex::lock() {
  auto mutex = get();
  if (mutex) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  }
}

void SpiBusMutex::unlock() {
  auto mutex = get();
  if (mutex == nullptr) {
    return;
  }

  xSemaphoreGiveRecursive(mutex);
}

SpiBusMutex::Guard::Guard() { lock(); }

SpiBusMutex::Guard::~Guard() { unlock(); }

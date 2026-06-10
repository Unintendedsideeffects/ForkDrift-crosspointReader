#pragma once
// Included before every simulator compilation unit via -include build flag.
// Provides missing stubs from the crosspoint-simulator library for ForkDrift-
// specific APIs (FreeRTOS static allocation, Arduino String extensions, etc.).
#if defined(SIMULATOR) && defined(__cplusplus)
#include <freertos/semphr.h>

typedef int StaticSemaphore_t;

inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t*) { return xSemaphoreCreateMutex(); }

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t*) { return xSemaphoreCreateMutex(); }

inline bool xSemaphoreTakeRecursive(SemaphoreHandle_t sem, uint32_t ticks) { return xSemaphoreTake(sem, ticks); }

inline bool xSemaphoreGiveRecursive(SemaphoreHandle_t sem) { return xSemaphoreGive(sem); }

inline void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; }

// pdMS_TO_TICKS: in the simulator delays are real milliseconds, so 1 tick = 1 ms.
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#include <esp_ota_ops.h>  // pulls in esp_partition_t, esp_err_t, and OTA stubs
#include <esp_system.h>   // esp_restart() stub

#define ESPMock _ignored_ESPMock
#define ESP _ignored_ESP
#include <Arduino.h>
#undef ESPMock
#undef ESP

struct ESPMock {
  uint32_t getFreeHeap();
  uint32_t getMinFreeHeap();
  uint32_t getHeapSize() { return 1024 * 1024; }
  uint32_t getMaxAllocHeap() { return 1024 * 1024; }
  void restart() {}
};
extern ESPMock ESP;
#endif

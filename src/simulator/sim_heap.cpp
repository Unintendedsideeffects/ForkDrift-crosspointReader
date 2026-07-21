#ifdef SIMULATOR

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>

#ifdef __APPLE__
#include <malloc/malloc.h>
#define MALLOC_USABLE_SIZE malloc_size
#else
#include <malloc.h>
#define MALLOC_USABLE_SIZE malloc_usable_size
#endif

// Smoke-test probe: ReaderOptionsActivity::render() records which layout mode it
// took so the smoke test can assert the half-screen preview path was used.
bool g_sim_reader_options_full_screen = false;

static std::mutex heap_mutex;
static uint32_t live_bytes = 0;
static uint32_t max_live_bytes = 0;
static uint32_t biggest_alloc = 0;
static uint32_t heap_budget = 330000;
static uint32_t max_alloc_budget = 330000;
static bool budget_initialized = false;

static void init_budget() {
  if (budget_initialized) return;
  budget_initialized = true;
  const char* env = std::getenv("SIM_HEAP_BUDGET");
  if (env) {
    heap_budget = static_cast<uint32_t>(std::atoi(env));
  }
  const char* max_env = std::getenv("SIM_HEAP_LARGEST");
  if (max_env) {
    max_alloc_budget = static_cast<uint32_t>(std::atoi(max_env));
  } else {
    max_alloc_budget = heap_budget;
  }
}

// Global operator new/delete overrides
void* operator new(std::size_t size) {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();

  if (size > biggest_alloc) biggest_alloc = size;

  void* ptr = std::malloc(size);
  if (!ptr) {
    fprintf(stderr, "SIM OOM: aborting on new of %zu bytes\n", size);
    std::abort();
  }

  uint32_t actual = MALLOC_USABLE_SIZE(ptr);
  if (live_bytes + actual > heap_budget) {
    std::free(ptr);
    fprintf(stderr, "SIM OOM: aborting on new of %zu bytes (live: %u, actual: %u, budget: %u)\n", size, live_bytes,
            actual, heap_budget);
    std::abort();
  }

  live_bytes += actual;
  if (live_bytes > max_live_bytes) max_live_bytes = live_bytes;
  return ptr;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();

  if (size > biggest_alloc) biggest_alloc = size;

  void* ptr = std::malloc(size);
  if (!ptr) return nullptr;

  uint32_t actual = MALLOC_USABLE_SIZE(ptr);
  if (live_bytes + actual > heap_budget) {
    std::free(ptr);
    return nullptr;
  }

  live_bytes += actual;
  if (live_bytes > max_live_bytes) max_live_bytes = live_bytes;
  return ptr;
}

void operator delete(void* ptr) noexcept {
  if (!ptr) return;
  std::lock_guard<std::mutex> lock(heap_mutex);
  uint32_t actual = MALLOC_USABLE_SIZE(ptr);
  live_bytes -= actual;
  std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept { ::operator delete(ptr); }

void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new[](std::size_t size, const std::nothrow_t& nt) noexcept { return ::operator new(size, nt); }
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { ::operator delete(ptr); }

// Provide the mocked ESP methods
uint32_t ESPMock::getFreeHeap() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget > live_bytes ? heap_budget - live_bytes : 0;
}

uint32_t ESPMock::getMinFreeHeap() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget > max_live_bytes ? heap_budget - max_live_bytes : 0;
}

uint32_t ESPMock::getMaxAllocHeap() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  // Provide realistic shrinking: either fixed override, or what's physically left
  uint32_t remaining = heap_budget > live_bytes ? heap_budget - live_bytes : 0;
  return max_alloc_budget > remaining ? remaining : max_alloc_budget;
}

extern "C" uint32_t esp_get_minimum_free_heap_size() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget > max_live_bytes ? heap_budget - max_live_bytes : 0;
}

struct SimHeapSummary {
  ~SimHeapSummary() {
    fprintf(stderr, "SIM HEAP SUMMARY: live=%u, low_water=%u, biggest_single=%u\n", live_bytes,
            heap_budget > max_live_bytes ? heap_budget - max_live_bytes : 0, biggest_alloc);
  }
} sim_heap_summary;

#endif

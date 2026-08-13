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
// Device-measured defaults, not round numbers. Seeded from a serial census of a real X4
// (2026-08-12, firmware 2b4ce85, Background Server = Always, USB attached):
//
//   ESP.getHeapSize()    177,256 - 181,880   -> total budget below
//   free at Home              43,036 - 54,908
//   free in the reader        ~40,000
//   ESP.getMaxAllocHeap()     17,396 - 40,948  -> largest-block budget below
//   lowest free ever seen          4,236
//
// The previous defaults were 330000 for BOTH, which modelled a heap 1.8x too large and a
// largest contiguous run 8-19x too generous. That is not a conservative error: it made the
// simulator structurally incapable of reproducing fragmentation failures, which are the
// dominant real-world class. The abort() root-caused on 2026-08-12 happened with 40 KB
// free and a largest block of 15,348 -- a state the old simulator could never enter, since
// it modelled free and largest as the same number.
//
// Largest-block is set to the low end of the observed range on purpose: the interesting
// bugs live where the device is busy, not idle.
static uint32_t heap_budget = 180000;
static uint32_t max_alloc_budget = 20000;
// Set when SIM_HEAP_BUDGET is given without SIM_HEAP_LARGEST: an explicit total with an
// implicit largest used to silently mean "no fragmentation at all".
static constexpr uint32_t kDefaultLargestBlockBytes = 20000;
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
    // NOT heap_budget. Defaulting largest == total models a perfectly unfragmented heap,
    // which no ESP32-C3 ever has; on device the two differ by 2-10x under load.
    max_alloc_budget = kDefaultLargestBlockBytes;
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

// Total, not free: the number ESP.getHeapSize() reports on device. Left inline at
// 1024*1024 in the mock it made the simulator self-contradictory -- "169,008 free of
// 1,048,576" reads as 84% headroom when the real state is a 180 KB budget nearly
// half spent. Absolute checks were unaffected, but anything reasoning about a
// ratio was silently wrong.
uint32_t ESPMock::getHeapSize() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget;
}

// Same budget as ESP.getFreeHeap(), deliberately: on device these two are the same
// quantity reached by different APIs, so any disagreement here is pure simulator
// artefact. See patch_simulator_hal.py 5e4 for why the stub could not stay inline.
uint32_t esp_get_free_heap_size() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget > live_bytes ? heap_budget - live_bytes : 0;
}

extern "C" uint32_t esp_get_minimum_free_heap_size() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  init_budget();
  return heap_budget > max_live_bytes ? heap_budget - max_live_bytes : 0;
}

// The largest single allocation seen this run -- the number that decides which sites are
// worth pooling. It lived only in the SimHeapSummary destructor below, which never runs:
// simulator_main ends with _exit(0) to skip global destructors (a deliberate fix for a
// shutdown race with the render task). So this was unreachable in practice; expose it.
uint32_t sim_heap_biggest_alloc() {
  std::lock_guard<std::mutex> lock(heap_mutex);
  return biggest_alloc;
}

struct SimHeapSummary {
  ~SimHeapSummary() {
    fprintf(stderr, "SIM HEAP SUMMARY: live=%u, low_water=%u, biggest_single=%u\n", live_bytes,
            heap_budget > max_live_bytes ? heap_budget - max_live_bytes : 0, biggest_alloc);
  }
} sim_heap_summary;

#endif

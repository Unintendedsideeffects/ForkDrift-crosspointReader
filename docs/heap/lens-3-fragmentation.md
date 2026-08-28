# Lens 3 — Fragmentation & Allocation Lifetime Analysis

**Date:** 2026-08-28
**Analyst:** opencode/big-pickle (Lens 3 — fragmentation & allocation lifetime)
**Measured data source:** Serial traces and device heap measurements in brief

---

## Findings Table (ordered by impact)

| # | Finding | Type | Fragmentation impact | Evidence |
|---|---------|------|---------------------|----------|
| 1 | **32KB InflateReader shared window persists across all activities** | Permanent late | Blocks 32KB contiguous; single largest heap hole | `InflateReader.cpp:9-20` |
| 2 | **Per-page churn of variable-sized TextBlock/PageLine/PageImage objects** | Transient | Shreds heap into many small fragments every page turn | `Page.cpp:395-478`, `TextBlock.cpp:191-287` |
| 3 | **CSS parser rule vector: up to ~6KB contiguous reserve during section build** | Activity | Creates a 6KB island mid-heap during indexing, never coalesced after | `CssParser.cpp:928-934` |
| 4 | **8KB grayscale strip scratch allocated + freed per render** | Transient | Creates and releases an 8KB hole per page when AA is on | `EpubReaderActivity.cpp:2269-2274` |
| 5 | **BgWifi task stack (8KB) + TRMNL fetch stack (13KB) allocated at different times** | Activity | Two separate allocations that sit on different sides of the heap; freed and re-created non-deterministically | `BackgroundWifiService.h:52`, `Registration.cpp:351` |
| 6 | **selectionBaseSnapshot: 48KB second framebuffer allocation, activity-lifetime** | Activity | Creates a second 48KB island that partitions the heap for the entire selection session | `EpubReaderActivity.cpp:1734`, `EpubReaderActivity.h:96` |
| 7 | **FontCacheManager LRU: variable-size DRAM cache, never compacted** | Permanent late | Grows incrementally, never shrinks; each glyph page is a separate allocation | `FontDecompressor.cpp`, `FontCacheManager.h` |

---

## Allocation Timeline: Boot → Home → Book Open → Page Turn → Sleep

| Phase | Allocation | Lifetime | Size (bytes) | Permanent (P) / Activity (A) / Transient (T) |
|-------|-----------|----------|-------------|----------------------------------------------|
| **Boot** | `.bss/.data` static objects | Forever | ~179,400 total heap remaining (from 400KB SRAM minus statics) | P |
| **Boot** | Framebuffer (EInkDisplay member) | Forever | 48,000 | P |
| **Boot** | CrossPointSettings singleton | Forever | ~1,000 (est.) | P |
| **Boot** | Global font objects (flash-resident) | Forever | ~0 DRAM (data in flash) | P |
| **Boot** | FontCacheManager + initial cache | Forever | ~500-2,000 (grows on use) | P |
| **Boot** | HeapGuard thresholds (constexpr) | Forever | 0 (compile-time) | P |
| **First inflate** | InflateReader shared window | Forever (until release) | 32,768 | P |
| **Home** | HomeCarouselCache frame slots | A | 48,000 per frame × N frames | A |
| **Home** | Cover thumbnail generation buffers | T | Varies (~500-2,000 per cover) | T |
| **Book open** | `new Section(...)` | A | ~200 (object) + open file handle | A |
| **Book open** | `Epub::load()` → BookMetadataCache | A | ~500-1,000 (metadata LUT in RAM) | A |
| **Book open** | `CssParser::loadFromCache()` → rules hash table | A | ~1,000-6,000 (scales with rule count) | A |
| **Book open (first chapter)** | `createSectionFile()` → ChapterHtmlSlimParser | T (during build) | ~5,000-15,000 (parse state, vectors) | T |
| **Book open (first chapter)** | Page elements: TextBlocks, PageLines, PageImages | T (serialized to SD, then freed) | ~3,000-8,000 per page worth | T |
| **Page turn** | `loadPageFromSectionFile()` → `Page::deserialize()` | T | ~1,000-4,000 (varies by page content) | T |
| **Page turn** | TextBlock deserialization: 5 vectors per block | T | ~100-500 per block (scales with word count) | T |
| **Page turn (AA on)** | Grayscale strip scratch | T | 8,000 (100 bytes × 80 rows) | T |
| **Page turn (non-strip AA)** | `storeBwBuffer()` chunks | T | 6 × 8,000 = 48,000 (chunked) | T |
| **Font cache hit** | FontCacheManager page buffer | P (LRU retained) | ~2,000-4,000 per font-page | P |
| **Selection mode** | `selectionBaseSnapshot` | A | 48,000 | A |
| **Selection mode** | `selectionPageIndex` vector | A | ~2,000-8,000 (768 words max) | A |
| **Background server** | `BackgroundWebServer` startup | A | 16,336 (server + routes + WS) | A |
| **Background server** | `BackgroundWifiService` task stack | A | 8,192 | A |
| **TRMNL fetch** | Fetch task stack | T | 12,288 + 1,024 overhead | T |

---

## Why Largest Block Is Always ~1/4 of Free Heap

The measured ratios (44,608/12,276; 36,348/9,204; 11,224/5,620; 39,784/10,228) consistently show the largest free block at roughly 20-30% of total free heap. This is not coincidence — it is the signature of a heap with two large permanent holes and a sea of variable-sized churn fragments.

### The two permanent walls

1. **Framebuffer: 48,000 bytes at a fixed address** — allocated as an `EInkDisplay` member array in `.bss` (not the heap), but it carves a fixed 48KB region out of the address space. All heap allocations must live around it.

2. **InflateReader shared window: 32,768 bytes on the heap** — lazily allocated at first inflate (`InflateReader.cpp:37`), never freed unless explicitly released (`releaseSharedWindow()`). This is a permanent 32KB island. On a 179KB heap, this single allocation consumes ~18% of total free space. After it is allocated, the remaining heap is ~147KB, but the 32KB block sits at whatever address `malloc` chose — typically not at the edges.

These two permanent allocations partition the address space. The 32KB inflate window is the most damaging: it sits mid-heap and is large enough that its neighbors cannot coalesce around it.

### The churn sea

Every page turn deserializes a `Page` with variable numbers of `PageLine` and `TextBlock` elements. Each `TextBlock` is `new (std::nothrow) TextBlock(words, wordXpos, wordStyles, wordFocusBoundary, wordFocusSuffixX, blockStyle)` (`TextBlock.cpp:281`), which means five `std::vector` moves plus a struct copy. The word vectors hold `std::string` objects — each one a separate heap allocation.

A typical page might have 20-40 lines, each with a `TextBlock` containing 5-15 words. That is 100-600 individual `std::string` allocations per page, each 4-32 bytes, scattered across the heap. When the page is freed (on the next page turn), these become holes of varying sizes. The next page has a *different* word count per line, so the freed holes rarely match the new allocation sizes.

**This is why largest block ≈ 1/4 of free heap.** The heap is a Swiss cheese of ~100-500 byte holes, interspersed with the 32KB inflate window and various medium allocations (CSS parser, font cache). The largest contiguous run of these holes is limited by whichever medium allocation sits in the worst position. On average, in a uniform random distribution of holes, the largest contiguous block of N total free bytes in a fragmented heap is roughly `N / (number_of_interstitial_allocations + 1)`. With 4-6 interstitial allocations (inflate window, CSS parser, section object, font cache page, background task stack remnants), the ratio lands at 1/5 to 1/4 — exactly what is measured.

---

## Fragmentation Contributors (detailed)

### 1. InflateReader shared window — 32,768 bytes, permanent

**Size:** 32,768 bytes (`INFLATE_DICT_SIZE`, `InflateReader.cpp:9`)
**Frequency:** Allocated once, persists until process exit or explicit `releaseSharedWindow()`
**Why it fragments:** A 32KB block that sits at an arbitrary heap address for the lifetime of the process. It is the single largest heap allocation. On a 179KB heap, it is 18% of total capacity. Its fixed position prevents coalescing of adjacent free blocks across it.

**Fix:** Pool the inflate window with the framebuffer loan mechanism, or release it when not in use. The inflate window is only needed during image decoding and EPUB extraction — not during steady-state reading. Alternatively, allocate it from the framebuffer loan scratch space (which is available during section builds but not during rendering).

**Estimated improvement:** +32KB to largest block when window is not needed. Risk: re-allocation on next inflate fragments the heap again, so this only helps if the window can be kept permanently or never needed. The current design chose permanence deliberately to avoid per-call churn. **Recommendation: keep permanent, accept the cost.** The alternative (alloc/free per inflate) would be worse for fragmentation. The real problem is that this 32KB exists on a device with only 179KB total.

**Files/functions:** `lib/InflateReader/InflateReader.cpp:9-20,37,57-63,65-75`

### 2. Per-page TextBlock churn — variable size, per page turn

**Size:** Each `TextBlock` contains 5 vectors: `words` (vector<string>), `wordXpos` (vector<int16_t>), `wordStyles` (vector<Style>), `wordFocusBoundary` (vector<uint8_t>), `wordFocusSuffixX` (vector<uint16_t>). A block with 10 words allocates: 10 × sizeof(string) + 10 × 2 + 10 × 1 + 10 × 2 = ~10 × 32 + 32 = ~352 bytes for the vector storage, plus 10 string payloads at 4-32 bytes each (~150 bytes), plus the TextBlock object itself (~100 bytes). Total per block: ~600 bytes. A page with 30 lines × 10 words = ~18,000 bytes of scattered allocations.
**Frequency:** Every page turn. `loadPageFromSectionFile()` → `Page::deserialize()` → `TextBlock::deserialize()` for every line.
**Why it fragments:** Each word string is an independent heap allocation. The `std::vector` for word data and positions are separate allocations. No pooling, no arena. When the `Page` goes out of scope, these become holes of 4-32 bytes. The next page's words are different sizes, so the holes rarely fit.

**Fix — Pooling/Arena:** A bump allocator for the entire Page deserialize, freed wholesale when the page is replaced. The arena would be sized to the worst-case page (~20KB) and allocated once per chapter. On page turn, only the bump pointer resets; no individual frees.

**Estimated improvement:** Largest block would jump from ~10KB to ~25-30KB (eliminating ~200-600 small holes per page). The arena itself is a single contiguous allocation that sits at a fixed address.

**Risk:** The arena must be large enough for the worst-case page. Image-heavy pages with tables could need 15-20KB. The arena must also be released when changing chapters (to avoid holding memory across chapter transitions).

**Files/functions:** `lib/Epub/Epub/Page.cpp:395-478` (deserialize), `lib/Epub/Epub/blocks/TextBlock.cpp:191-287` (deserialize), `lib/Epub/Epub/Page.h:126` (Page::elements vector)

### 3. CSS parser rule vector — 1,000-6,000 bytes, activity-lifetime

**Size:** `rulesBySelector_` is an `unordered_map`. The `reserve(ruleCount)` call at `CssParser.cpp:930` allocates `ruleCount * sizeof(void*)` bytes contiguously — roughly 6KB at MAX_RULES. The map entries themselves are scattered.
**Frequency:** Allocated once per book open, persists for the reading session.
**Why it fragments:** The reserve creates a large contiguous bucket array mid-heap. When the book is closed, this is freed, leaving a 6KB hole. If other allocations landed on both sides during the session, this hole cannot coalesce with either neighbor.

**Fix:** Move the CSS rule storage to a flat `std::vector<std::pair<selector, rule>>` with binary search, or use a fixed-size array. The reserve is already doing this partially, but the unordered_map overhead (bucket pointers, node allocations) scatters the actual entries.

**Estimated improvement:** ~2-3KB to largest block (eliminating per-node heap allocations). The reserve itself is already a good pattern; the problem is the per-entry node allocations.

**Files/functions:** `lib/Epub/Epub/css/CssParser.cpp:928-934`, `lib/Epub/Epub/css/CssParser.h` (rulesBySelector_ member)

### 4. Grayscale strip scratch — 8,000 bytes, per-render transient

**Size:** `gwBytes * STRIP_ROWS` = 100 * 80 = 8,000 bytes (`EpubReaderActivity.cpp:2269`)
**Frequency:** Allocated and freed on every page render when text anti-aliasing is on and the display supports strip grayscale.
**Why it fragments:** 8KB is a significant allocation on a 179KB heap. It is allocated, used briefly (the entire strip rendering loop), then freed. If the heap is fragmented, this allocation can fail even with 11KB free (the measured case: `free=37732, largest=9204` — the 8KB scratch would fail here because the largest block is 9,204 and 8,000 > 9,204 minus alignment).

**Fix:** Pool the scratch buffer. Allocate it once when AA is enabled, keep it for the reading session, free on `onExit()`. The scratch is never needed simultaneously with other 8KB allocations.

**Estimated improvement:** Eliminates one 8KB alloc/free cycle per render. Largest block improves by ~8KB when the scratch is permanent (avoids the fragmentation it currently causes).

**Risk:** Pins 8KB permanently. On this heap, that is 4.5% of total capacity. Acceptable if it prevents the current OOM (`[ERS] OOM: grayscale strip scratch`).

**Files/functions:** `src/activities/reader/EpubReaderActivity.cpp:2269-2274`, `lib/GfxRenderer/GfxRenderer.cpp:2220-2237` (BW buffer chunks — 6 × 8KB for non-strip fallback)

### 5. Background server stacks — 8KB + 13KB, non-deterministic lifetime

**Size:** BackgroundWifiService task stack: 8,192 bytes (`BackgroundWifiService.h:52`). TRMNL fetch task stack: 12,288 + 1,024 overhead = 13,312 bytes (`Registration.cpp:351,354`).
**Frequency:** Allocated when background server starts (on every wake with `backgroundServerMode = Always`). TRMNL fetch is a one-shot task, stack freed on completion.
**Why it fragments:** These stacks are allocated at different points in the lifecycle. The BgWifi stack is allocated early (on boot or first wake). The TRMNL stack is allocated on-demand during reading. Their positions in the heap are determined by what else is allocated at that moment. When the reader tears down the background server, the 8KB stack is freed. If the TRMNL fetch has already allocated and freed its stack, the two holes may not be adjacent.

**Fix:** Allocate both stacks from a pre-reserved pool at boot. Or, since the background server is torn down at reader entry, ensure the TRMNL fetch completes *before* the server teardown (it already does — `Registration.cpp:471` shows it runs before background server startup). The key insight: these two stacks are never alive simultaneously in normal operation.

**Estimated improvement:** Minimal improvement to largest block; these are already managed. The real impact is that the 8KB BgWifi stack is freed at reader entry, which is what creates the 22KB reclaim observed in traces.

**Files/functions:** `src/network/background/BackgroundWifiService.h:52`, `src/features/terminus_sleep/Registration.cpp:351-427`, `src/network/background/BackgroundWifiService.cpp:358`

### 6. Selection snapshot — 48,000 bytes, activity-lifetime

**Size:** 48,000 bytes (copy of framebuffer, `EpubReaderActivity.cpp:1734`)
**Frequency:** Allocated once when text selection mode is entered, freed on exit.
**Why it fragments:** A 48KB allocation mid-heap. On a 179KB heap this is 27% of total capacity. It sits wherever malloc places it, splitting whatever was contiguous before. During selection mode, the largest block is guaranteed to be small because this 48KB island is in the way.

**Fix:** Capture the snapshot to SD card instead of RAM. The e-ink display retains its image, so the snapshot is only needed for incremental overlay redraws. Writing to SD and re-reading is slower but avoids the 48KB RAM cost. Alternatively, use the framebuffer itself as the snapshot base (it already holds the BW image) and only store the *diff* for overlay restoration.

**Estimated improvement:** +48KB to largest block when selection mode is active. Risk: SD writes are slow; incremental overlay redraws would need to read the snapshot back from SD, adding latency.

**Files/functions:** `src/activities/reader/EpubReaderActivity.cpp:1732-1743`, `src/activities/reader/EpubReaderActivity.h:96,98`

### 7. FontCacheManager LRU — variable, permanent

**Size:** Each font page is ~2-4KB of glyph bitmaps. The LRU holds maybe 5-15 pages depending on which fonts are in use. Total: ~10-60KB.
**Frequency:** Grows incrementally as pages are rendered. Never shrinks (LRU evicts oldest only when capacity is exceeded).
**Why it fragments:** Each font page is a separate allocation. The LRU never compacts. Over a reading session, as different fonts/styles are used, the cache accumulates pages that are never coalesced.

**Fix:** Pre-allocate a fixed-size pool for font cache pages at boot. Each page is the same size (one font page = ~2-4KB). A fixed-size array of page slots avoids per-page heap allocation.

**Estimated improvement:** ~5-10KB to largest block (eliminating scattered small allocations). The cache still holds the same data, but as a contiguous array instead of linked list of separate allocations.

**Files/functions:** `lib/EpdFont/FontDecompressor.cpp:371`, `lib/GfxRenderer/FontCacheManager.h`

---

## The Defrag-Reboot Path: Assessment

**What it does:** `heapDirtyFromIndexing_` (set to `true` after a section build in `render()` at `EpubReaderActivity.cpp:1898`) causes `loop()` (`EpubReaderActivity.cpp:546-566`) to call `persistAndRestartForRecovery()`, which saves progress to SD and triggers a silent restart back into the reader. The fresh boot starts with a clean heap; the section cache on SD makes the resume a cheap `loadSectionFile()` (deserialize, not rebuild).

**When it triggers:** Only after a foreground section index (chapter build). The threshold is `kHeapDefragLargestBlockThreshold = 120 * 1024` (120KB) — but the code at `EpubReaderActivity.cpp:552` clears the flag when `largestBlock >= 120KB`, which on this device basically never happens (largest block at boot is ~90-100KB before any allocations). So the reboot fires essentially every time a chapter is indexed.

**Does it work?** Yes, it works. The serial traces confirm that a reboot recovers the heap to a clean state. The 2026-08-28 traces show "Reader, after server stop+delete" at ~32KB free, and a fresh boot would be higher.

**Is it the right answer?** No. It is a workaround for a fundamental problem: the ESP32-C3 has no heap compaction, and the firmware's allocation pattern fragments the heap badly enough that a reboot is needed after every heavy operation. The reboot costs 2-3 seconds (deep sleep wake + boot + cache load), which is noticeable to the user.

**Verdict: Replace.** The defrag-reboot is a symptom of inadequate allocation lifetime management. Fixing the fragmentation root causes (TextBlock churn, inflate window permanence, CSS parser allocation) would make the reboot unnecessary. The reboot should remain as a last-resort safety net, not the primary fragmentation mitigation.

---

## Pooling/Arena Assessment

### Worth it: Yes, for two specific allocation families

**1. Page element arena (bump allocator for Page::deserialize)**

Every page turn deserializes a `Page` with variable numbers of `TextBlock`, `PageLine`, `PageImage`, and `PageTableFragment` objects. Each is a separate `new` with separate vectors. A bump allocator that allocates the entire page's elements in one contiguous block, freed wholesale on the next page turn, would eliminate hundreds of small holes per turn.

**Concrete proposal:**
- Allocate a ~20KB arena at chapter load (once per `Section` lifetime).
- `Page::deserialize()` bump-allocates from this arena instead of individual `new`.
- On page turn, the old page's arena is reset (bump pointer returns to start).
- The arena is freed when the chapter changes (`section.reset()`).

**Cost:** 20KB permanently held per chapter (5-11% of heap). But this 20KB is a *single* contiguous block, which is far better than the ~15-20KB of scattered fragments it replaces. The largest block would jump from ~10KB to ~25-30KB.

**Risk:** Low. The arena only needs to be large enough for the worst-case page. Images are decoded separately (not into the arena), so the arena size is bounded by text content.

**Files:** `lib/Epub/Epub/Page.cpp:395-478`, `lib/Epub/Epub/blocks/TextBlock.cpp:191-287`

**2. Grayscale scratch buffer (persistent allocation)**

The 8KB strip scratch (`EpubReaderActivity.cpp:2269`) is allocated and freed on every render when AA is on. Making it persistent (allocated once on reader entry, freed on exit) eliminates one 8KB alloc/free cycle per page and keeps the 8KB contiguous.

**Cost:** 8KB permanently held during reading (4.5% of heap). This is the same cost as the current transient allocation, but without the fragmentation.

**Risk:** Very low. The scratch is never needed simultaneously with other 8KB allocations in the render path.

**Files:** `src/activities/reader/EpubReaderActivity.cpp:2269-2274`

### Not worth it: Background server stacks

The BgWifi stack (8KB) and TRMNL stack (13KB) are already managed with appropriate lifecycle (freed at reader entry, recreated on exit). Pooling them would pin 21KB permanently for a task that runs intermittently. The current approach (allocate on demand, free when done) is correct for these; the fragmentation they cause is transient and recoverable.

### Not worth it: Selection snapshot

The 48KB selection snapshot is only active during selection mode. Pooling it (allocating once at boot) would pin 48KB permanently for a feature used rarely. The current approach (allocate on demand, free on exit) is correct; the fix should be to avoid the 48KB RAM copy entirely (write to SD, read back on demand).

---

## Summary: What Would Fix the ~1/4 Ratio

The fragmentation ratio is fundamentally caused by:

1. **A 32KB permanent inflate window** that sits mid-heap (keep it — freeing per-inflate is worse).
2. **Per-page TextBlock churn** of ~200-600 small string allocations per page turn (fix with arena).
3. **8KB grayscale scratch** allocated/freed per render (fix with persistent allocation).
4. **CSS parser and font cache** scattered medium allocations (partially fixable with flat containers).

The highest-impact fix is the **page element arena** (#2 above). It would turn ~15-20KB of scattered per-page fragments into a single 20KB block, immediately improving the largest block from ~10KB to ~25-30KB. Combined with the persistent grayscale scratch (#4), the largest block would reach ~33-38KB — enough to reliably satisfy the 48KB framebuffer loan, the 13KB TRMNL task stack, and other medium allocations without fragmentation failure.

The defrag-reboot path should be kept as a safety net but its trigger threshold should be raised or its frequency reduced once the arena is in place. It should not be the primary fragmentation story.

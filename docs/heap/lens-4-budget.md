# L4 — State-by-state heap budget map

**2026-08-29 occupancy:** Home + Always serving is **29–36 KB free / 17 KB
largest** after UDP `"hello"`, not the 11 KB / 5.6 KB figures in this file.
`SERVER_STARTUP_BYTES` is 4,504, not 16,336. This lens is the 2026-08-28
diagnosis. Current firmware: [../HEAP_ANALYSIS.md](../HEAP_ANALYSIS.md)
occupancy addendum.

Lens: replace the stale `HeapGuard.h` assumption ("steady-state reading heap ~60–130 KB free") with an accounting of what this ESP32-C3 actually has, and what is resident in each observed state.

Firmware tree: `/home/malcolm/Code/ForkDrift/crosspoint-reader`. Traces: `scratchpad/heap/traces/01`–`04`. Device settings in play: `backgroundServerMode = Always` (forces `wifiAutoConnect`), USB-CDC attached. **No build was run; no `.map` file was produced.** Linker RAM `128,244 / 327,680` is taken from the brief (PlatformIO summary). Runtime heap numbers are from the four serial traces.

---

## Findings (impact order)

| # | Finding | Kind | Impact |
|---|---|---|---|
| 1 | `kLowFloorBytes = 60 KB` and the header's "~60–130 KB free while reading" are not a state this device occupies. Reading is 36–40 KB free; Home + Always-server is 11 KB. Every luxury gated on the LOW floor is dead in the measured default config. | **measured** floors vs traces; **inferred** which call sites die | Binding. Sizing against 60–130 KB will ship features that never run. |
| 2 | Always-mode background web server is the largest *runtime* heap consumer. Home without server objects: 47,440 free. Home with server: 11,148. Teardown returns ~22 KB. Combined with book-open while the server is still up, this is the path that produced global min-free **3,848**. | **measured** | Default setting puts the device near-death on Home and overlaps the EPUB load. |
| 3 | Fragmentation, not total free, is the limit that matters. Observed pairs: 44,608 free / 12,276 largest (reader, stuck session); 11,148 free / 5,620 largest (Home+server); 40,428 free / 10,228 largest (reader, post-fix). A 48 KB heap buffer cannot be allocated in any captured state. | **measured** | Second framebuffer, cover cache, carousel RAM slot, menu page preview are all geometrically impossible here. |
| 4 | The 32,768-byte DEFLATE window is reserved for process lifetime on the first deflated EPUB open. That is ~18% of the heap pool, still held after leaving the reader. | **inferred** size from code; **measured** reservation on book open | Permanent tax on every subsequent state in a session that opened a deflated book. |
| 5 | Several policies still charge for a 48 KB framebuffer *plus* 60–96 KB of headroom (`ReaderOptionsMemoryPolicy`, `HomeCoverCachePolicy` via `kLowFloorBytes`, grayscale scratch via `canAllocate(..., kLowFloorBytes)`). Those checks cannot pass at 36–47 KB free. | **inferred** arithmetic on current constants; **measured** refusals in traces | AA, font prewarm, menu preview, Home cover RAM cache, carousel RAM frames all skip in the measured Always-server session. |
| 6 | Static DRAM's single named whale is the 52,272-byte `frameBuffer0`. Dual-buffer would add another 52,272. Fonts, i18n, icons, hyphen tries, HTML are flash, not RAM — easy to mis-count. | **inferred** from `const`/`constexpr` placement; framebuffer is BSS | Flash feature flags (Bookerly, languages) do not buy reading-heap. `EINK_DISPLAY_SINGLE_BUFFER_MODE=1` is load-bearing for DRAM. |
| 7 | ~20 KB of the linker DRAM remainder never appears in `ESP.getHeapSize()`, and `getHeapSize()` itself shrinks ~4 KB while the server is up. The budget does not add up to the last byte. | **measured** totals vs brief's 128,244/327,680; gap **inferred** | Do not treat 327,680 − 128,244 as "the heap". |

---

## 1. The budget (327 KB SRAM region → static → heap → per-state)

### 1.1 Reconciling the two "how much RAM" numbers

ESP32-C3 chip SRAM is 400 KB. That is **not** the heap. PlatformIO reports:

```
RAM: 39.1% (used 128,244 bytes from 327,680)
```

`327,680` (`0x50000`) is the ESP-IDF/Arduino-ESP32 **DRAM segment** (`dram0_0_seg`) for `.data` + `.bss` + heap. The other ~72 KB of the 400 KB is instruction cache plus IRAM-resident code (WiFi/BT/ISRs). I did not have a linker map, so the IRAM split is **inferred** as `400,000 − 16,000 I-cache − 327,680 DRAM ≈ 56 KB IRAM`, not measured.

`128,244` is static `.data` + `.bss` in that DRAM segment (brief; not re-measured). Remaining DRAM on paper:

```
327,680 − 128,244 = 199,436 bytes
```

Runtime `ESP.getHeapSize()` from the traces is **not** 199,436. It is:

| Trace / state | `ESP.getHeapSize()` (Total) | Source |
|---|---|---|
| Reader, stuck session | **179,416** | `01-stuck-page-turns.log:14` |
| Home + server (after first start) | **175,240** | `02-exit-reader-reclaim.log:91` |
| Home + server (book-open session) | **175,192** | `03-open-book-degrade.log:1` |
| Reader after server teardown | **179,368** | `03-open-book-degrade.log:54` |
| Reader post-fix | **179,576 … 179,592** | `04-post-fix-page-turns.log:43,65,86` |

So the heap *pool* is ~175–180 KB, typically **~179,400** with the server down. Gap vs linker leftover:

```
199,436 − 179,416 = 20,020 bytes
```

**Unknown (cannot decompose without a `.map` and `heap_caps` dump):** that ~20 KB is some mix of (a) multi_heap block headers not counted in `total_free + total_allocated`, (b) DRAM reserved at the top of `dram0_0_seg` and never given to the heap, (c) capability pools that `getHeapSize()` does not include. A confident split would be a lie.

`getHeapSize()` is also **not constant**. Server-up totals are ~4,176 bytes lower (`179,416 − 175,240`). That is **measured**. Mechanism **unknown** — if it were ordinary `malloc`, Total would stay put and Free would fall. Something in the server/WiFi/mDNS path is removing bytes from the pool the Arduino API reports, or the two sessions differ by USB/WiFi internal registrations.

`src/simulator/sim_heap.cpp:26–44` already documented a 2026-08-12 census in the same band (`getHeapSize()` 177,256–181,880; reader ~40,000 free; min-free 4,236). The 2026-08-28 traces sit on top of that, not in a new universe. `HeapGuard.h:34–36` was not updated to match.

### 1.2 Static DRAM consumers (what 128,244 is made of)

Only one object is large enough to name with a hard size. Everything else is either flash or too small / too opaque without a map.

| Resident | Bytes | Placement | Kind | Citation |
|---|---|---|---|---|
| `EInkDisplay::frameBuffer0` | **52,272** | `.bss` (member of global `HalDisplay display`) | **inferred** | `open-x4-sdk/.../EInkDisplay.h:38` `MAX_BUFFER_SIZE = 52272`; `:158` `uint8_t frameBuffer0[MAX_BUFFER_SIZE]`; `lib/hal/HalDisplay.cpp:5` `HalDisplay display`; `lib/hal/HalDisplay.h:86` embeds `EInkDisplay`. Used size at runtime is 48,000 (`BUFFER_SIZE` = 800×480/8, `EInkDisplay.h:33`) but the array is sized for the X3 max. |
| `frameBuffer1` | 0 in this build | would be `.bss` | **inferred** | `#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE` at `EInkDisplay.h:160–162`. `platformio.ini:33` sets `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1`. Dual-buffer would add **+52,272 DRAM**. |
| Font bitmaps, glyph tables, kern, ligatures | hundreds of KB | **flash** (`.rodata`) | **inferred** | `static const uint8_t …Bitmaps[N]` e.g. `lib/EpdFont/builtinFonts/ubuntu_10_regular.h:11` (18,686 B) and `static const EpdFontData ubuntu_10_regular` at `:3167`. `const` data on ESP32-C3 is flash-mapped, not DRAM. |
| `EpdFont` / `EpdFontFamily` globals | small (pointer wrappers) | `.bss` | **inferred** | `src/BuiltinFonts.cpp:11–18` and following. Each `EpdFont` is one pointer (`lib/EpdFont/EpdFont.h:8`). Dozens of objects: order **0.5–2 KB**, not tens of KB. Count depends on `ENABLE_*_FONTS`. |
| I18n string blobs (all languages) | large | **flash** | **inferred** | `lib/I18n/I18nStrings.cpp:1856` `const char STRINGS_EN_DATA[]` (file is ~73k lines, one blob per language). `const` → flash. |
| Icon bitmaps | tens of bytes each | **flash** | **inferred** | `static const uint8_t Book24Icon[]` in `src/components/icons/book24.h:5`. |
| Hyphenation tries (9 languages) | ~flash tens of KB each | **flash** | **inferred** | `constexpr uint8_t en_trie_data[]` `lib/Epub/Epub/hyphenation/generated/hyph-en.trie.h:9`; `sizeof(en_trie_data)` referenced at `:1434`. Same pattern for `hyph-{uk,ru,it,fr,es,de,sv,pl}`. `LanguageHyphenator` objects in `LanguageRegistry.cpp:20–27` are small DRAM. |
| Web HTML (`PROGMEM`) | flash | **flash** | **inferred** | `constexpr char …[] PROGMEM` under `src/network/html/*.generated.h`. |
| `heaptrace::Mark[20]` | **400** | `.bss` | **inferred** | `lib/Memory/HeapTrace.h:18,27–36` (`kCapacity * sizeof(Mark)`, documented as 400). |
| `CrossPointSettings` singleton | ~1–2 KB | `.bss` | **inferred** | Fixed `char` members: `sleepPinnedPath[256]` `src/CrossPointSettings.h:309`, plus `userFontPath[128]`, `installedOtaFeatureFlags[192]`, etc. `:402–440`. No large RAM buffer. |
| WiFi / NimBLE / newlib / FreeRTOS statics | **unknown** remainder of 128,244 | `.bss` | **inferred** | 128,244 − 52,272 framebuffer − ~2–4 KB of named small globals ≈ **~70–75 KB unaccounted static**. This is almost certainly ESP-IDF + Arduino + NimBLE (compiled in even when idle) + LwIP statics. Without a `.map`, do not invent a split. |

**Fonts are the classic mis-count.** `docs/BUILD_CONFIGURATION.md` "Size Impact" figures (Bookerly ~1055 KB, Noto Sans ~1009 KB, …) are **flash**. Enabling a font pack does not take a megabyte of DRAM. It adds a handful of `EpdFont` objects and, at render time, a heap **hot group** (measured 7,515 B in `04-post-fix-page-turns.log:11`).

### 1.3 Heap pool, then per-state resident (must add up)

Let `pool = ESP.getHeapSize()` and `used = pool − free`.

```
Chip SRAM              400,000     (datasheet; not in traces)
  I-cache + IRAM      ~72,320     inferred; not in PlatformIO "RAM"
  DRAM segment        327,680     brief (PlatformIO RAM denominator)
    static .data/.bss 128,244     brief
      framebuffer      52,272     inferred (EInkDisplay.h:38,158)
      other static     75,972     unknown mix
    leftover          199,436     arithmetic
      heap pool       175,192–179,592   measured (traces Total)
      unaccounted      ~20,000     unknown (see 1.1)
        per-state used = pool − free   table below
        per-state free                 table below
```

The rows **do not close to the last byte**. The honest remainder is the ~20 KB pool-vs-leftover gap plus the ~76 KB unnamed static. Both are "ESP-IDF / Arduino / radio BSS and allocator accounting", not application caches.

---

## 2. State table

Free / largest / Total from `ESP.getFreeHeap()` / `ESP.getMaxAllocHeap()` / `ESP.getHeapSize()` as logged by `src/main.cpp:919–920` and `src/activities/ActivityManager.cpp:145–147,196–198`.

| State | Free | Largest | Total (pool) | Heap used (pool−free) | Resident subsystems | Source |
|---|---|---|---|---|---|---|
| Boot / `setup()` return | **unknown** | unknown | unknown | unknown | Display begin, fonts registered, ActivityManager render task (8,192 B stack, `ActivityManager.cpp:65–66`), loop task (Arduino default; **not read from sdkconfig**). `heaptrace::mark` exists for this (`HeapTrace.h:6–16`) via `CMD:HEAPTRACE`; **these traces do not contain a dump**. | no trace |
| Home idle, WiFi up, **server not yet started** | **47,440** | **13,812** | ~(179,416 from prior reader) | ~132 KB | Home activity (just constructed), WiFi STA still up (`keepWifi=true` from reader, `EpubReaderActivity.cpp:349–352`), inflate window if a deflated book was opened this boot (see §3). Cover RAM cache **not** taken; carousel **disk-only**. | **measured** `02-exit-reader-reclaim.log:9` (`enter Home`); carousel skip `02:12` `heap 45328` |
| Home + background server (Always) | **11,148** | **5,620** | **175,192** | **164,044** | Previous, plus `bgwifi` task 8,192 B (`BackgroundWifiService.h:52`), `CrossPointWebServer` routes+WS+UDP (~16 KB startup, see §3), mDNS. TRMNL fetch **deferred**. Shelf refresh **skipped**. Cover buffer released. | **measured** `03-open-book-degrade.log:1`; TRMNL `02:29`; shelf `02:50`; covers `02:55` `heap 11092` |
| Home + server, mid book-open (EPUB loaded *before* Home exits) | **8,788** | **5,620** | — | — | Previous **plus** `Epub` + metadata cache + CSS parser (then released) + **32,768 B inflate window**. This is the overlap. | **measured** `03:13` `exit Home: free=8788 min=3896 largest=5620`; inflate `03:8` |
| Reader entry, **before** server teardown | **10,172** | **5,620** | — | — | Home deleted (~+1.4 KB vs 8,788), server **still up**. Global min-free already **3,848**. | **measured** `03:15` |
| Reader, immediately after server stop+delete | **32,064–32,068** | — | — | — | Reader + EPUB + inflate window; server objects gone. | **measured** `03:25–26` |
| Reader first-page render (degraded) | dips to **24,656** mid-render | **9,204** | — | — | Font prewarm **skipped** (heap 33,136 < 40,000). Selection index **refused**. Grayscale scratch **OOM**. Then settles. | **measured** `03:36–51` |
| Reader steady (stuck session, pre-fix 45 KB gate) | **36,348** then **44,608** on later turns that refused | **9,204** then **12,276** | **179,368 / 179,416** | **143,020 / 134,808** | Reader + section cache + inflate window + FDC hot group. No server. WiFi kept. Page turns **refused** at 44,608 (old gate). | **measured** `03:54`; `01:14,39` |
| Reader steady (post-fix) | **40,340–40,428** | **10,228** | **179,576–179,592** | **~139,160** | Same shape; page turns **succeed**. Prewarm still skipped (heapBefore 31–34 KB). AA still skipped. Menu preview dropped on the way in (CONTEXT: free=39,784 / largest=10,228). | **measured** `04:43,65,86`; prewarm `04:27,49,69`; AA `04:41,61,81`; menu exit `04:3` |
| Reader + menu | **39,784** at menu exit | **10,228** | — | — | Menu activity on the stack; 48 KB preview **not** retained (`ReaderOptionsMemoryPolicy` cannot pass). | **measured** `04:3`; policy **inferred** `ReaderOptionsMemoryPolicy.h:13–28` + `EpubReaderMenuActivity.cpp:104–113` |
| Library browse | **unknown** | unknown | unknown | unknown | `MyLibraryActivity::blocksBackgroundServer() = true` (`MyLibraryActivity.h:79`) so Always-server should tear down like the reader. Heap should resemble reader-without-book or Home-without-server. **No trace.** | **inferred** |
| Sleep | **unknown** | unknown | unknown | unknown | Blocks server (`SleepActivity.h:51`). Timed-refresh path **tears WiFi down** and waits until `canAllocate(44 KB, 16 KB headroom)` (`BackgroundWifiCoordinator.cpp:245–260`). Image decode is transient. **No sleep trace in this set.** | **inferred** |
| OTA / foreground web server | **unknown** | unknown | unknown | unknown | `OtaUpdateActivity` worker stack **16,384** (`OtaUpdateActivity.cpp:154`) plus WiFi. Foreground `CrossPointWebServerActivity` also blocks the background server (`CrossPointWebServerActivity.h:82`). **No trace.** | **inferred** |

`ESP.getMinFreeHeap()` is monotonic since boot (`HeapTrace.h:13`). Values in the table are the watermark *so far*, not "free at this instant".

---

## 3. Deltas between states (what actually moved)

### 3.1 Leave reader → Home (no server yet) — **measured**

`02-exit-reader-reclaim.log:6–9`:

```
exit EpubReader:  free=44272  largest=12276
enter Home:       free=47440  largest=13812
delta             +3168       +1536
```

~3 KB matches "leaving the reader reclaims only ~3 KB" in CONTEXT. The 32 KB inflate window is **not** released (`InflateReader.cpp:17,57–62`: process-lifetime once claimed; `releaseSharedWindow` only if idle). So Home after a book still pays that tax.

### 3.2 Home starts Always-mode server — **measured**

From `02:12` (carousel, heap 45,328) through `02:26–50`:

| Step | Free | Δ | Citation |
|---|---|---|---|
| After carousel skip | 45,328 | | `02:12` |
| TRMNL defer (task already spawned) | 37,732 / largest 9,204 | ~−7,596 vs 45,328 | `02:29`. Matches `TASK_STACK = 8192` (`BackgroundWifiService.h:52`) plus TCB/malloc overhead, **inferred**. |
| WEB before begin | 33,320 | −4,412 | `02:31`. Part of this is `onBackgroundNetworkReady` / TRMNL attempt; not fully attributed. |
| WebServer allocation | 33,016 | **−304** | `02:33`. Matches `BackgroundServerPolicy.h:79` (304). |
| Route setup | 20,988 | **−12,028** | `02:35`. Policy comment said 11,832 (`BackgroundServerPolicy.h:81`). |
| `server.begin()` (WS + UDP) | 16,996 | **−3,992** | `02:47`. Policy comment 4,200 (`BackgroundServerPolicy.h:82`). |
| After mDNS + settle | 11,224 … 11,268 | ~−5,700 | `02:50,91` |

Net Home-enter 47,440 → Home+server 11,148 = **−36,292**.

Teardown of the *server objects* (not WiFi, not necessarily the task):

```
before stop:     10,892
after stop():    20,820   +9,928
after delete:    33,216   +12,396
recovered                    22,324
```

(`02:60–67`). Matches CONTEXT "~22 KB" and `EpubReaderActivity.cpp:297`.

`getHeapSize()` 179,416 → 175,240 = **−4,176** while the server is up (`01:14` vs `02:91`).

### 3.3 Open book on Home+server — **measured** (worst overlap)

`03-open-book-degrade.log:1–26`:

```
Home+server              free=11148  largest=5620  min=3896
EPUB load (still Home)   inflate reserved; CSS skip reserve(0)
exit Home                free=8788   (−2,360 for Epub+caches)
enter EpubReader         free=10172  (+1,384 Home deleted)  min=3848
WEB before stop          15872
after delete server      32068       (+16,196 from before-stop)
```

The **3,848** watermark appears on the enter line (`03:15`). `getMinFreeHeap()` is since-boot; the dip happened during Home destructor + Reader constructor + `onEnter` *before* the WEB stop log, with **Home leftovers + Always-server + WiFi + the just-loaded EPUB + inflate window** all live. That is the worst-case overlap the traces contain.

`canAllocate(0)` at largest=5,620 fails the 32 KB floor (`HeapGuard.cpp:49–54`: `free - bytes < floorAfter`), which is why CSS logs `Skipping reserve(0) (~0 B): low heap (largest=5620)` (`03:9`; code `CssParser.cpp:928–933`). It is a floor check, not a "zero-byte allocation".

### 3.4 Reader onEnter teardown → first page → steady — **measured**

`EpubReaderActivity::onEnter` stops both background servers *before* layout (`EpubReaderActivity.cpp:336–357`). Trace 03 shows that the EPUB *load* already ran on Home (file browser / confirm path), so teardown is late for the load and on time for the first render.

```
skip font prewarm     heap=33136     (gate 40000, EpubReaderActivity.cpp:88,2169)
SELECTION fallback    words=85 bytes=3503 free=24656 largest=9204   (03:40)
OOM gray scratch      8000 bytes     (03:51)
Rendered              settle 36348 / largest 9204 / min 3848        (03:54)
```

Mid-render dip 33,136 → 24,656 = **−8,480** temporary (section/page structures). Then free *rises* to 36,348: temporaries released; FDC `pageBuf=0` (prewarm skipped) but a later session shows `hotGroup=7515` (`04:11`).

Post-fix page turns (`04`) sit at **40,340–40,428** free, **10,228** largest, min-free 12,956 (that boot never went as low). Prewarm still skipped every turn (`heapBefore` 28,716–33,980 < 40,000). AA still skipped every turn.

### 3.5 Inflate window — **inferred** size, **measured** trigger

`lib/InflateReader/InflateReader.cpp:9` `INFLATE_DICT_SIZE = 32768`. `ensureSharedWindow()` mallocs it once (`:65–74`). `Epub::load` reserves when `hasDeflatedEntries` (`lib/Epub/Epub.cpp:451–461`). Trace `03:8`: `Inflate window: reserving (cached hasDeflatedEntries=1)`. Comment at `InflateReader.cpp:17`: kept for process lifetime. **Not freed on reader exit** (the +3 KB Home reclaim would be ~35 KB if it were).

### 3.6 What the Always-server start gate thinks vs what the traces did

Current formula (`BackgroundServerPolicy.cpp:17–18`, `BackgroundWifiService.h:52`):

```
startMinFreeBytes(8192) = 8192 + 16336 + 12288 + 4096 = 40,912
runningMinFreeBytes()   = 12288 + 4096               = 16,384
```

`spawnTask` *does* call `canStartNow()` (`BackgroundWifiService.cpp:328–330`). Home-enter 47,440 ≥ 40,912 and largest 13,812 ≥ 8,192, so the **first** start is consistent.

The **restart** at `02:81` happens at free ≈ 33,216, which is **below 40,912**. Either that firmware's constants differed from this tree, or something else passed the gate. **Unresolved.** Do not paper over it.

The Always-mode server then *runs* at 11,148 free, **below** `runningMinFreeBytes` 16,384. That floor lives on `BackgroundWebServer.cpp:392` (USB/on-charge instance), **not** on the `BGWIFI` `CrossPointWebServer` loop. So Always-mode is allowed to sit at 11 KB. **Inferred** from the two classes; **measured** that it does sit there.

---

## 4. Worst-case path (closest to zero)

**Sequence (measured in `03`, watermark 3,848):**

1. Home + Always background server + WiFi STA (free ~11 KB, largest ~5.6 KB).
2. User confirms a book. `Epub::load` runs **while Home and the server are still alive** (`03:6–12` before `exit Home`).
3. 32,768 B inflate window taken against a 5,620 B largest-block heap (`03:8–9` — window reservation logged; whether that malloc succeeded into a hole or fragmented further is not logged).
4. Home exits, Reader is constructed, `onEnter` begins. Watermark **3,848** (`03:15`).
5. Only then does `BG_WIFI.stop` run (`03:17`). Recover ~22 KB. First page renders in the degraded band (no prewarm, no AA, no selection index).

**Resident at the 3,848 moment (inferred from that sequence, not a heap dump):** WiFi STA buffers + `bgwifi` task stack 8,192 + web server routes/WS/mDNS + Home remnants being destroyed + `Epub` + `BookMetadataCache` + `CssParser` + inflate window 32,768 + Reader object + the 52,272 B static framebuffer (BSS, not heap) + FDC/Arduino/LwIP baseline.

A *plausible worse* path, **not captured**: the same overlap **plus** Home cover RAM cache (48,000) **plus** carousel RAM frame (48,000). Those are now gated (`HomeCoverCachePolicy.h:45–61`, `HomeCarouselCache.h:52–53`) and **did not allocate** in these traces (`02:12` disk-only; cover released at `02:55`). The policy comments record that the ungated cover malloc *was* the historic low-water driver (`HomeCoverCachePolicy.h:17–26`: post-malloc free 15,240 / largest 9,204). If those gates ever pass in Always-mode, they would stack on the 11 KB Home and undercut 3,848.

Sleep/OTA/TLS were not in these traces. TLS wants `MIN_HEAP_FOR_HTTPS = 38,000` (`HttpDownloader.h:24`) — that is already the *entire* reader free heap, so HTTPS from Home+server is structurally refused (`02:50` shelf skip is a different 84 KB gate).

---

## 5. Feature-flag RAM cost

`config/features.yaml` `estimated_size_kib` and `docs/BUILD_CONFIGURATION.md` "Size Impact" are **flash**, measured by `scripts/measure_feature_sizes.py` (firmware image bytes). They are the wrong unit for this lens. Below: **heap/DRAM** as far as the code and traces allow. "0 persistent" means no extra working set when the feature is idle; transients still exist when the feature *runs*.

| Flag | Est. RAM (persistent unless noted) | Notes |
|---|---|---|
| *(baseline, all builds)* | 52,272 DRAM framebuffer | `EInkDisplay.h:38,158`. Not a flag. |
| `EINK_DISPLAY_SINGLE_BUFFER_MODE=0` | **+52,272 DRAM** | Would compile `frameBuffer1` (`EInkDisplay.h:160–162`). Currently forced on (`platformio.ini:33`). |
| `ENABLE_BACKGROUND_SERVER` + Always | **~22 KB server objects + 8,192 task + WiFi STA (see below)** | **Measured** server teardown 22,324 B (`02:60–67`); task 8,192 (`BackgroundWifiService.h:52`). WiFi STA cost **not isolated** in these traces (radio never off). Prior comment "~50 KB WiFi stack" at `HomeCoverCachePolicy.h:34` is **inferred**/old. `sim_heap.cpp:30` Home 43–55 KB with Always+USB is consistent with "WiFi up, server not yet dominating". |
| `ENABLE_BACKGROUND_SERVER_ON_CHARGE` | same objects, only while USB | Same CrossPointWebServer; different coordinator. USB-path has a 16,384 running floor (`BackgroundWebServer.cpp:392` + `BackgroundServerPolicy.cpp:21`). |
| `ENABLE_BACKGROUND_SERVER_ALWAYS` | **makes the 11 KB Home the default** | This is the setting in the traces. Turning it off is the single largest heap win available without deleting features. **Measured** 47 KB vs 11 KB Home. |
| `ENABLE_TERMINUS_SLEEP` | 0 idle; **13,312** for fetch task | `TRMNL_FETCH_TASK_STACK + OVERHEAD = 12288+1024` (`Registration.cpp:351–354`). **Measured** defer: `02:29` `no room for the 13312 B task stack (free=37732, largest=9204)` — largest 9,204 < 13,312 contiguous. |
| `ENABLE_BLE_PAGE_TURNER` / `ENABLE_BLE_WIFI_PROVISIONING` | **unknown** BSS even idle (inside the 128,244); heap when `NimBLEDevice::init` (`BlePageTurner.cpp:82`, `BleWifiProvisioner.cpp:74`) | Init is activity-scoped, not boot. **No BLE trace.** NimBLE is not free; do not claim 0. |
| `ENABLE_LUA_PLUGINS` | 0 in default (`FeatureFlags.h:267–268` defaults 0); VM heap when `LuaManager::begin` | Logs heap before/after (`LuaManager.cpp:649,678`). **No measurement here.** |
| `ENABLE_*_FONTS` | **~0 DRAM** for bitmaps (flash); **+small** `EpdFont` objects; **+~7.5 KB heap** hot group while rendering | **Measured** `hotGroup=7515` (`04:11`). Flash sizes in `features.yaml` (1055/1009/804/…). |
| `ENABLE_USER_FONTS` | SD font caches on use | `SdCardFont` prewarm buffers; skipped when reader heap < 40 KB. **Not sized.** |
| `ENABLE_HYPHENATION` | **~0 DRAM** for tries (constexpr flash); small `LanguageHyphenator` objects | 9 tries under `lib/Epub/Epub/hyphenation/generated/`. |
| `ENABLE_MARKDOWN` | 0 idle; parser buffers when opening `.md` | Flash ~210 KB (`features.yaml`). **No MD trace.** |
| `ENABLE_BOOK_IMAGES` | 0 idle; transient decode (PNG `sizeof(PNG)` + 16 KB headroom gate, `PngToFramebufferConverter.cpp:86–90`; JPEG similar). Pixel bands capped 24 KB (`PixelCache.h:60`) | Transients. Inflate window 32 KB if the image is deflated inside the EPUB (already reserved for the book). |
| `ENABLE_IMAGE_SLEEP` | 0 idle; transient PNG/JPEG at sleep | Sleep path tries to free WiFi first (`BackgroundWifiCoordinator.cpp:234–260`). **No trace.** |
| `ENABLE_XTC_SUPPORT` | transient page buffer ~W×H/8 (or ×2 at 2-bit) | `XtcReaderActivity.cpp:210–220`. **No trace.** |
| `ENABLE_OTA_UPDATES` | **16,384** worker stack while in OTA activity | `OtaUpdateActivity.cpp:154`. Plus WiFi. **No trace.** |
| `ENABLE_KOREADER_SYNC` / OPDS / Calibre | 0 idle; HTTPS wants **38,000** free (`HttpDownloader.h:24`) | Will not run on Home+server (11 KB) or comfortably on reader (40 KB). Font download wants 48,000 + 24,000 largest (`FontDownloadActivity.cpp:77–80`) — also dead in these states. |
| `ENABLE_REMOTE_KEYBOARD_INPUT` | flash ~12 KB; hotspot is a WiFi-AP heap spike | **Not measured.** |
| `ENABLE_WIFI_CLOCK` | 4,096 B `TimeSync` task (`test_stack_budgets.cpp:50–51`) | Transient NTP. |
| Themes / Pokemon / dark mode / stats / notes / anki / bookmarks | **~0 extra DRAM** beyond code+flash assets | Icons are `static const`. Activity objects are heap-allocated and freed on exit (`CLAUDE.md` / `ActivityManager`). |
| `ENABLE_EPUB_SUPPORT` | core; inflate 32,768 when a deflated book is opened | See §3.5. |

Profiles (`config/features.yaml:674–759`): **lean** is core only (no Always-server). **standard** enables `background_server` + on-charge, not Always. **full** enables `background_server_always`. The traces are a **full-like Always** configuration, which is also `platformio.ini` `[env:default]` (`ENABLE_BACKGROUND_SERVER_ALWAYS=1` at `platformio.ini:120`). That is why the stale 60–130 KB comment and the default firmware disagree.

---

## 6. How to size a new feature on this device

Replacement for `HeapGuard.h:34–36`. Practical rule, not a patch (this lens does not implement).

**The numbers to design against (X4, 2026-08-28, Always-server, USB attached):**

| Design-against | Value | Why |
|---|---|---|
| Heap pool | **175–180 KB** (`ESP.getHeapSize()`) | Not 380 KB, not 327 KB. Those are chip / DRAM-segment. |
| Reading, server down, WiFi held | **36–40 KB free, 9–10 KB largest** | `03:54`, `04:86` |
| Home + Always server | **11 KB free, 5.6 KB largest** | `03:1` |
| Global min-free seen | **3.8 KB** | `03:15` |
| Contiguous 48 KB from heap | **never, in any captured state** | max largest = 13,812 (`02:9`) |
| Bytes you may `malloc` during reading *and* still leave `kCriticalFloorBytes` (32 KB) | `free − 32768` → **~3.5 KB** at 36,348; **~7.7 KB** at 40,428 | `HeapGuard.cpp:49–52` |

**Rules:**

1. **Persistent working set ≤ ~8 KB contiguous**, or it does not belong on the heap while Always-server or the reader is up. Larger needs: the existing 52 KB **static** framebuffer, flash, or SD. Do not malloc a second framebuffer (`HomeCoverCachePolicy.h` and `EpubReaderMenuActivity.cpp:100–113` already learned this; the gates now refuse, which is why covers/preview are skipped).

2. **Do not gate optional work on `kLowFloorBytes` (60 KB).** In every measured Always-server and reading state, `heapguard::pressure()` is already `Low` or `Critical` (`HeapGuard.cpp:38–46`). `canAllocate(n, kLowFloorBytes)` needs `free ≥ n + 61440`. Grayscale scratch is 8,000 B (`EpubReaderActivity.cpp:2265–2275`, `gwBytes * STRIP_ROWS` = 100×80) so it wants **≥ 69,440 free**. That is why every post-fix page logs `OOM: grayscale strip scratch (8000 bytes)` (`04:41,61,81`) even with 40 KB free and a 10 KB largest block that would fit 8,000 **if the floor were 0**.

3. **`kCriticalFloorBytes` (32 KB) is slightly below observed reading free (36–40 KB)** and far above Home+server (11 KB). It is a plausible "reader must still paint" line. It is **not** a "Home+Always is healthy" line. Features that must run on Home with Always-server have to work in **5–6 KB contiguous** or not run.

4. **Charge the real allocation, not a 48 KB phantom.** `ReaderOptionsMemoryPolicy` wants 96,000 free **and** 48,000 largest, plus another 48,000 to retain a preview (`ReaderOptionsMemoryPolicy.h:13–28`) → 144,000 / 96,000. Heap pool is 180 KB with ~140 KB already used. Preview is **always** dropped (`04` CONTEXT line; code `EpubReaderMenuActivity.cpp:106–113`). Font prewarm at 40,000 (`EpubReaderActivity.cpp:88`) is *just* at post-render free and *below* in-render free, so it skips every turn in `04`.

5. **Assume fragmentation.** Size against `largestBlock()`, not `freeBytes()`. `canAllocate` already does both (`HeapGuard.cpp:49–54`). A feature that needs 12 KB contiguous will fail on Home+server (largest 5,620) and on post-fix reading (largest 10,228).

6. **If you open a deflated EPUB, budget −32,768 for the rest of the boot** (`InflateReader.cpp:9,17`). Stored-only books skip it (`Epub.cpp:451–457`).

7. **Compile flags that change this budget in anger:** `ENABLE_BACKGROUND_SERVER_ALWAYS` (11 KB Home vs ~47 KB), dual framebuffer ( +52 KB static DRAM), BLE init (unknown heap), Lua VM, OTA 16 KB stack, Terminus 13 KB stack. Font packs and extra languages do not.

8. **Verify on device with Always-server on.** Simulator default `heap_budget = 180000`, `max_alloc_budget = 20000` (`sim_heap.cpp:44–45`) is in the right *band* but still ~2× too generous on largest-block vs Home+server's 5,620.

**Worked example:** a new "keep the last page as a bitmap for animations" feature wants 48,000 B. Against this budget: largest block never exceeds 13,812 in traces → **refuse**. Use the static `frameBuffer0` or an SD cache (Home carousel already does disk-only at `02:12`). A new "8 KB glyph scratch" might fit post-fix reading on largest-block (10,228) but fails `canAllocate(8000, kCriticalFloorBytes)` at 40,428 free (`40000 − 8000 = 32000` which is **not** `> 32768`; at 40,428 the max floor-respecting alloc is 7,660 B). So even 8 KB is over the critical-floor budget while reading. Either lower the floor, or take the 8 KB from a static pool, or accept going through the floor for that page.

---

## 7. Unknowns (explicit)

- Boot-to-first-Home heap, and Home **without** a prior book (inflate window absent). `CMD:HEAPTRACE` would answer; these traces do not include it.
- WiFi-STA-only cost with server off and inflate window off. Radio never off in this set. `HomeCoverCachePolicy.h:34` "~50 KB" is not re-measured here.
- The ~20 KB linker-leftover vs `getHeapSize()` gap, and the ~4 KB `getHeapSize()` shrink while the server runs.
- The ~76 KB of static DRAM that is not `frameBuffer0`.
- Why BGWIFI restarted at ~33 KB free against a current `startMinFreeBytes` of 40,912.
- Library, sleep, OTA, BLE-init, Lua, Markdown, HTTPS-fetch heap. Code sizes only.
- Exact `CONFIG_ARDUINO_LOOP_STACK_SIZE` (Arduino-ESP32 default is typically 8,192; not confirmed from a sdkconfig in this tree).

---

## Arithmetic appendix (gates vs observed free)

| Gate | Constant | Passes at reader 40,428 / largest 10,228? | Passes at Home+server 11,148 / 5,620? |
|---|---|---|---|
| `kLowFloorBytes` | 61,440 (`HeapGuard.h:37`) | no | no |
| `kCriticalFloorBytes` | 32,768 (`HeapGuard.h:38`) | yes (barely) | no |
| Font prewarm | 40,000 (`EpubReaderActivity.cpp:88`) | **in-render heapBefore is 32–34 KB → no**; post-render yes | no |
| Grayscale scratch via `canAllocate(8000, kLowFloorBytes)` | need ≥ 69,440 free | no (**measured** OOM) | no |
| `canAllocate(8000, kCriticalFloorBytes)` | need ≥ 40,768 free | **no** (40,428 − 8,000 = 32,428 < 32,768) | no |
| Menu preview `canRetainPreview` | 96,000+48,000 free and 48,000+48,000 largest | no | no |
| Cover RAM cache `kFloorAfterBytes = 60 KB` | need ≥ 48,000+61,440 = 109,440 free | no | no |
| Carousel RAM frame | 48,000+4,096 (`HomeCarouselCache.h:52–53`) | no | **measured** skip at 45,328 (`02:12`) |
| BGWIFI start | 40,912 free and 8,192 largest | n/a (reader blocks server) | first start from 47,440 **yes**; running at 11,148 **n/a** |
| BGWIFI running floor (other class) | 16,384 | n/a | Always-path **does not apply this**; stays at 11 KB |
| Library shelf | 84,000 (`BackgroundWifiService.h:64`) | n/a | **measured** skip (`02:50`) |
| TRMNL fetch stack | 13,312 contiguous | n/a | **measured** defer, largest 9,204 (`02:29`) |
| HTTPS | 38,000 (`HttpDownloader.h:24`) | borderline/no | no |
| CSS `MIN_FREE_HEAP_FOR_CSS` | 48,768 (`CssParser.cpp:55`) | no | no |

That table is the stale-comment problem in one grid: policies written for 60–130 KB free, running on a device that reads at 36–40 KB and idles on Home at 11 KB.

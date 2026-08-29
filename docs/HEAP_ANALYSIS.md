# Heap analysis — measured, 2026-08-28; occupancy diet 2026-08-29

Authoritative accounting of memory on the X4 (ESP32-C3, no PSRAM), produced by a
four-lens static audit plus on-device profiling over USB serial. It replaces the
guesswork in `lib/Memory/HeapGuard.h`, whose floors are documented as tuned for
"observed steady-state reading heap (~60-130KB free)". **That figure is wrong by
roughly a factor of two**, and a large family of thresholds derives from it.

Every number below labelled *measured* came off the device. Numbers labelled
*inferred* were read out of the source and are marked as such.

The 2026-08-28 tables are the diagnosis. The 2026-08-29 occupancy diet changed
who holds memory at Home+Always; that addendum is current firmware truth.

## TL;DR

1. **Real reading heap is 36-40 KB free, not 60-130 KB.** Every threshold tuned
   against the old figure now fires always or never, rather than when intended.
2. **The whole book load happens before the background server is torn down.**
   That overlap is still true. It used to run at ~11 KB free; after the occupancy
   diet the server is cheaper, so the overlap is no longer structurally 11 KB.
3. **Home with Always-server was 11 KB free (2026-08-28) because the HTTP stack
   held ~84 `server->on()` objects, mDNS, and WebSockets.** After the 2026-08-29
   diet it is **29–36 KB free / 17 KB largest** once UDP `"hello"` has started
   WS+mDNS. That 11 KB figure is occupancy, not the floor of a healthy server.
4. **Fragmentation is a step change at book open, not a slow drift.** Largest
   block collapses at open, then plateaus. Page turns do not leak.
5. **Page turns are heap-neutral.** There is no per-page leak. Measured flat
   across five consecutive turns.
6. **Two destructive misattributions of OOM survive**, one of which deletes a
   valid on-disk cache.
7. **AA no longer needs an 8 KB heap scratch.** Page-turn grayscale loans the
   framebuffer and restores BW by repainting. Font prewarm gate is 24 KB. On-device
   Settings streams one tab. Inflate’s 32 KB window and Home’s 48 KB cover clone
   remain the large process-lifetime / luxury tells.

## The budget

| Layer | Bytes | Source |
|---|---:|---|
| ESP32-C3 SRAM (linker DRAM) | 327,680 | build report |
| Static `.data`/`.bss` | 128,244 | build report (`RAM: 39.1%`) |
| — of which one framebuffer | 52,272 | `EInkDisplay.h:38,158` (`MAX_BUFFER_SIZE`) |
| Runtime heap pool | **175,272 - 179,464** | measured, `CMD:HEAPPROF` |

`327,680 - 128,244 = 199,436` on paper, but the heap API reports 175-180 KB.
**That gap is now resolved** (linker-map analysis, 2026-08-28, see
`plans/heap-2026-08-28/linker-analysis.md`): raw internal heap-capable memory is
**191,280 bytes**, and `ESP.getHeapSize()` reports less because TLSF allocator
bookkeeping and light heap poisoning are excluded — and **grow with the number of
live blocks**.

That is independently corroborated by the measurements in this document: 380
allocated blocks reports a 179,464-byte total, 642 blocks reports 175,272. 262
extra blocks cost 4,192 bytes, i.e. ~16 bytes of allocator header per block.
So the "total heap" figure is not a constant, and **none of that ~12-16 KB is
reclaimable** — it is the price of having allocations at all.

Note the framebuffer is **static, not heap**, and there is only one:
`EINK_DISPLAY_SINGLE_BUFFER_MODE=1` (`platformio.ini:33,227`). Any proposal to
"free the framebuffer" is proposing to change static allocation, not heap.

## Measured state table

Captured with `scripts/device_walk.py run … ` using the `heap` verb. `frag%` is
`1 - largest/free`: the share of free heap unreachable to any single allocation.

| State | free | largest | frag | free blocks | alloc blocks | min free |
|---|---:|---:|---:|---:|---:|---:|
| Boot, Home, server not yet up | 51,464 | 36,852 | 28% | 10 | 597 | 33,208 |
| Book open | 44,840 | 17,396 | 61% | 22 | 380 | 15,560 |
| After page turn 1 | 36,360 | 9,204 | 75% | 23 | 382 | 15,560 |
| After page turn 2 | 36,360 | 9,204 | 75% | 23 | 382 | 15,560 |
| After page turn 3 | 36,320 | 9,204 | 75% | 23 | 382 | 15,560 |
| Reader menu open | 35,664 | 9,204 | 74% | 22 | 386 | 15,560 |
| Menu closed | 36,320 | 9,204 | 75% | 23 | 382 | 15,560 |
| Two more page turns | 36,320 | 9,204 | 75% | 23 | 382 | 15,560 |
| **Left reader -> Home + server** | **11,228** | **5,620** | 50% | 8 | 642 | 7,792 |
| Home settled | 11,268 | 5,620 | 50% | 9 | 641 | 6,940 |

Read this table twice. The two things it says are not the obvious ones:

- **Page turns cost nothing.** 36,360 -> 36,360 -> 36,320 -> 36,320 -> 36,320.
  Whatever is wrong, it is not a leak in the reading path.
- **Leaving the book is the expensive operation, not entering it.** Free heap
  falls by 25 KB when you return to Home, because the background web server
  starts. The device spends its idle life in its worst memory state.

That last sentence is the 2026-08-28 occupancy picture. After the 2026-08-29
diet, returning to Home still starts the server, but serving idle is 29–36 KB
rather than 11 KB. See the occupancy addendum below.

### Post-fix baseline (re-measured 2026-08-28, after the session's commits)

The table above is the *pre-fix* device and is kept as the record of the original
diagnosis. Re-measured on the same hardware after `70fcdfce2`, `6cd1329a4`,
`116bb9ad3`, `c7a49de23` and `fbc42a088`:

| State | free | largest | frag | before |
|---|---:|---:|---:|---|
| Home + background server | 11,500 | **8,180** | 29% | 11,268 / 5,620 |
| Book open (warm cache) | 36,476 | 15,860 | 57% | 44,840 / 17,396 |
| Reading, steady | 36,460 | **15,860** | 57% | 36,320 / 9,204 |

**Free heap is essentially unchanged; contiguity is not.** The largest allocatable
block is up 46% at Home and 72% while reading. That is the number that was
failing allocations, so it is the one worth tracking.

### Occupancy diet (measured 2026-08-29)

Hunt-3 P0–P2c plus the start-budget retune. Device `192.168.86.51`, Always-server,
UDP discovery `"hello"` on 8134. Detail: `docs/FINDINGS.md` 2026-08-29T10:16Z.

What moved:

- ~84 `server->on()` heap handlers → one `WebRouteTableHandler` + flash `const`
  table of function pointers. Plugins register `WebRouteSpec[]`. Host tests still
  use `mountAll()` → mock `on()`.
- Background `begin(Background)` does not start mDNS or WebSockets. UDP 8134
  still binds. First `"hello"` starts WS, then mDNS, then replies. File Transfer
  / Calibre still start those themselves.
- Font-upload 4 KB buffer allocated on first WRITE, freed on END/ABORT.
- `SERVER_STARTUP_BYTES` 16,336 → 4,504 (dropped the measured 11,832 route line;
  kept WebServer + WS/UDP remainder). Always’s start gate is ~29 KB, not 40,912.
- RUNNING low-heap no longer calls `scheduleRetry()` (that disconnects WiFi).
  `evaluateRunningHeap` only aborts below `OBSERVED_RUNNING_MIN_FREE_BYTES`
  (4860) with `StopKeepWifi`.
- Settings POST/serial floor is 16 KB with reclaim-then-apply. On-device
  Settings UI streams the current tab via `forEachSetting` (no 48 KB rebuild gate).
- CSS whole-file skip is only below `kCriticalFloorBytes` (32 KB). Per-rule
  `canAllocate` still applies. Incomplete parse is still not cached.
- Page-turn AA paints grayscale into the live framebuffer and restores BW by
  re-rendering (`ReaderUtils::renderAntiAliased`). No 8 KB strip scratch, no
  `storeBwBuffer`. Font prewarm runs at ≥ 24 KB free.
- Host tests: `WebRouteRegistry::mountAll()` is compiled only for
  `CROSSPOINT_HOST_BUILD` / `SIMULATOR` and still binds mock `on()`. Firmware
  has a single live path (`WebRouteTableHandler`).
- Follow-on (2026-08-29/30): destroy Home before `Epub::load`; Reader Options
  overlay streams without the 96/48 KB reboot gate; post-index Continue at
  ≥ 14 KB largest (no `persistAndRestart`); cover thumbs and library shelf are
  luxury (shelf floor 38 KB HTTPS); STORED-shadow writes a method-0 ZIP beside
  cache and is not on the load path.

| State | Before (2026-08-28) | After occupancy flash |
|---|---|---|
| Home, Always not yet up | ~33–37 KB | 37,348 free / 15,860 largest (first flash, port 80 refused: gate still 40,912) |
| Home + Always serving | 11.5 KB / 8.2 KB largest | **29,392–36,640 free / 17,396 largest** after UDP hello |
| `/api/status` 1 Hz, 35 s | flap / WiFi disconnect | 35/35 HTTP 200, wifi `Connected` |
| `/api/settings` GET | 503 at 11 KB | 200, 67 entries, ~36 KB free |
| Reader after open + page turn | CSS often skipped | 42,080 free / 14,836 largest; min-free during open 9,436; **AA no longer allocates an 8 KB scratch** (re-paint restore; device confirm after this flash) |

First flash after P0 but **before** the start-budget update left Home at 37 KB
free with TCP `ECONNREFUSED` on port 80. Dropping the deleted 12 KB route line
from the start gate is what let Always come up.

### The cold-cache path, measured for the first time

Everything above — and every fix verified during that work — used a book whose
sections were already indexed on SD. The expensive path is a *cold* open: full
metadata build, chapter index, CSS parse and image extraction at once. Measured
by deleting the book cache and reopening:

- **min free fell to 4,164 bytes** during cache deletion and the cold reopen, the
  lowest figure seen since the original 3,848. The cold path, not the reading
  path, is where this device comes closest to the floor.
- The long index ends in the **silent defrag restart** (`heapDirtyFromIndexing_`,
  `EpubReaderActivity.cpp:547-568`). That is by design, and it is why uptime resets
  mid-open; a frame captured during the transition is blank.
- `ParsedText`'s OOM guard fired twice mid-index (`need=416 free=33,148` and
  `free=32,448`), dropping words. Those blocks are lossy but non-empty, so they are
  cached — the deliberate trade at `Section.cpp:434-437`.
- The CSS cache guard from `fbc42a088` was confirmed working end-to-end:
  `Not caching CSS rules: 0 rule(s) parsed but at least one stylesheet was skipped
  (free=46,664); will retry on next open`. This was previously verified only by
  build and host tests.

**The measurement the CSS retune needed.** At the moment CSS parsing is attempted
on a cold open, free heap is **46,664** and **45,264** bytes, against a threshold
of 65,536. So the gap is ~19 KB, and a threshold near 40,000 would let stylesheets
parse on this hardware. That number was previously unknown, which is why the
retune was deferred rather than guessed.

### Fragmentation is an event, not a trend

Largest block: 36,852 at boot -> 17,396 on book open -> 9,204 after the first
render -> 9,204 forever after. It is two discrete events, then a plateau. The
earlier degraded session sat at 44,608 free / 12,276 largest, i.e. the same
plateau shape at a different offset.

Consequence: a "wait for it to recover" or "retry later" strategy cannot work.
The heap does not recover within a session. Only a reboot restores contiguity,
which is why a defrag-reboot path exists at all
(`EpubReaderActivity.cpp:547-568`).

## Root causes

### 1. The document is loaded before the server is torn down

`createActivity()` calls `core::loadDocumentNoThrow<Epub>()`
(`src/features/epub/Registration.cpp:30`), which runs `doc->load()` — the full
EPUB parse, CSS parsing and the 32 KB inflate window — and only then constructs
`EpubReaderActivity`. The activity's `onEnter()`, which stops the background
servers, runs later still (`ActivityManager.cpp:194-199`).

So the entire book load executes at Home-with-server heap: **~11 KB free, 5,620
largest**. Measured min-free during this transition: **3,848 bytes**.

`EpubReaderActivity.cpp:309-323` states the intent plainly — "Both background
servers must be down BEFORE the book load allocates, not after… the ordering is
the whole fix." For layout that is true. For `Epub::load()` it is not: that has
already happened by the time `onEnter()` runs. **The stated invariant is not
achieved.** This is the highest-value fix on the list.

### 2. The always-on background server owns ~22 KB of a 179 KB heap

Measured directly across stop/delete: free went 10,892 -> 33,216, a **22,324
byte** recovery (`traces/02`). `backgroundServerMode` defaults to `Always`
(schema default 2), which also forces `wifiAutoConnect=1`.

It also starts and then keeps running below its own system floor: it settles at
11,148-11,268 free with a 5,620 largest block, under `kCriticalFloorBytes`
(32,768) — the value `HeapGuard.h` calls "the do-not-cross line for system
stability". There is no running-floor check for Always mode after startup.

### 3. Thresholds calibrated against a heap that does not exist

`kLowFloorBytes = 61,440` is **above every measured state on the device**. Every
feature gated on "Low pressure" is therefore permanently disabled, not
gracefully degraded. Confirmed measured casualties:

| Gate | Value | Measured outcome |
|---|---:|---|
| `MIN_HEAP_FOR_CSS_PARSING` (`Epub.cpp:22`) | 65,536 | never parses; trace shows `0 rules` |
| `MIN_FREE_HEAP_FOR_CSS` (`CssParser.cpp:55`) | 49,152 | style lookups return empty |
| `kMinHeapForSettingsApply` (`main.cpp:1053`) | 48,000 | settings API refuses in every state but the boot window |
| settings POST (`SettingsHandlers.cpp`) | 48,000 | refuses at 11 KB server state |
| shelf refresh (`BackgroundWifiService.h`) | 84,000 | skipped, logged |
| font prewarm (`EpubReaderActivity.cpp:88`) | 40,000 | skipped every open |
| AA strip scratch | 8,000 contiguous | fails every page; renders without AA |
| Terminus fetch task | 13,312 contiguous | deferred |

**CSS deserves emphasis: books are being laid out without their stylesheets.**
That is a silent rendering-fidelity regression, not just a memory statistic. And
the empty result is written to the section cache as if it were complete, so it
persists after memory recovers.

A worked example of the deadlock this creates: applying the setting that would
free 22 KB requires 48,000 free, which is unreachable *because* the server is
holding the 22 KB. Measured: `SETTINGS_ERR:low heap (12404 free)`.

### 4. OOM is still reported as data corruption — destructively

`Page::deserialize` returns `nullptr` for **both** corrupt data and insufficient
heap (`Page.cpp:408-411`, "insufficient heap for page elements").
`Section::loadPageFromSectionFile` cannot tell them apart, so it logs
"cache payload is corrupt" and calls `clearCache()`
(`Section.cpp:599-603`) — which **deletes the on-disk section file**
(`Section.cpp:266-282`), forcing a full re-index. Re-indexing is the most
allocation-hungry operation the reader has, attempted at the exact moment memory
is scarce.

This is the same misattribution class as the "Page load error" fixed in
`0f851eff4`, except this one destroys work. A transient low-memory moment costs
the user their cache.

### 5. Non-EPUB readers do not block the background server

`blocksBackgroundServer()` is overridden by `EpubReaderActivity`, Home,
MyLibrary, Settings, Sleep, OTA, WiFi and others — but **not** by
`TxtReaderActivity`, `XtcReaderActivity` or `MarkdownReaderActivity`
(`Activity.h:67` default is `false`). Those readers therefore run at the ~11 KB
server state. XTC needs a 48,000-byte contiguous page buffer to render at all,
against a measured largest block of 5,620.

## Ranked recommendations

Ordered by measured value per unit of risk. Items 1–7 are the 2026-08-28
analysis, not the occupancy diet. Hunt-3 P0–P2c plus the start-budget retune
landed 2026-08-29 and is the current Always-server occupancy story; see the
addendum above. That diet does not tear the server down before `Epub::load()`,
does not change the `Always` default, and does not fix AA scratch.

| # | Change | Est. recovery | Risk | Why |
|---|---|---:|---|---|
| 1 | Tear down the background server **before** the document factory runs, not in `onEnter()` | ~22 KB at the moment it matters most | Med | Fixes the stated-but-unmet invariant; would let CSS parsing and the inflate window run with headroom. Touches activity lifecycle ordering. |
| 2 | Re-tune `kLowFloorBytes`/`kCriticalFloorBytes` to the real 36-40 KB steady state, and migrate hand-rolled constants onto them | Unlocks correct degradation everywhere | Med | Restores the intended "luxuries defer, core function proceeds" semantics. Needs per-site review, not a blind constant swap. |
| 3 | Give `Page::deserialize`/`TextBlock` a typed failure (OOM vs corrupt) and stop deleting the cache on OOM | Prevents data loss | Low | Small, local, and stops a destructive misattribution. |
| 4 | Add `blocksBackgroundServer()` to TXT/XTC/Markdown readers | ~22 KB while reading those formats | Low | One-line override each; matches EPUB's existing behaviour. |
| 5 | Reconsider `backgroundServerMode` default (`Always` -> `Only on Charge`) | ~22 KB at idle | Low, but a product decision | Biggest single number, but it changes advertised behaviour. **User's call, not an engineering one.** |
| 6 | Do not persist a CSS cache built under a heap refusal | Correctness | Low | Prevents a transient low-heap moment baking wrong layout in permanently. |
| 7 | Arena/bump allocator for per-page elements; persistent AA scratch | Largest block ~10 KB -> ~33 KB (inferred) | High | Proposed by the fragmentation lens; unverified, and the largest change here. Prototype and measure before committing. |

Recommendations 1-4 and 6 are the ones supported by measurement. 5 is a product
decision. 7 is a hypothesis worth testing, not a conclusion.

## How to size a new feature on this device

This is the practical replacement for the stale comment in `HeapGuard.h`.

- Budget against **36-40 KB free / ~9–16 KB largest block** while reading, and
  **29–36 KB free / ~17 KB largest** at idle Home + Always after UDP hello
  (2026-08-29 occupancy). The 11 KB / 5.6 KB Home figure is the pre-diet
  occupancy state, not the current default.
- A persistent heap allocation must fit in **~8 KB contiguous**. There is no
  state in which a 48 KB heap buffer is available. If you need framebuffer-sized
  memory, borrow the framebuffer or stream via SD.
- **Check `largestBlock()`, not just `freeBytes()`.** At the measured plateau,
  75% of free heap cannot serve any single allocation.
- Put constant tables in flash (`static constexpr`), not DRAM. Fonts, i18n
  strings, hyphenation tries and icons already are.
- Prefer `heapguard::canAllocate(bytes, floor)` over a new constant. If you find
  yourself writing `if (ESP.getFreeHeap() < 45000)`, that is the bug this
  document exists to describe.
- Degrade, do not refuse: the renderer works correctly at 25 KB by skipping
  anti-aliasing and the font prewarm. Refusing a core operation wedges the
  device; degrading it does not.

## Reproducing this

Instrumentation added in this pass:

```bash
# One snapshot: free, largest, min, total, and block counts
uv run python scripts/device_walk.py heapprof

# Or sample labelled states inside a walk; writes heap.csv to the outdir
#   heap <label>
uv run python scripts/device_walk.py run scripts/walks/heap-profile.walk --outdir runs/heap
```

`CMD:HEAPPROF` (`src/main.cpp`) returns `heap_caps_get_info` as JSON, including
**free/allocated block counts** — the numbers that distinguish fragmentation
from exhaustion. `heapguard::freeBlockCount()`/`allocatedBlockCount()` expose the
same to firmware code. The pre-existing `CMD:HEAPTRACE` still covers the boot
window, which serial cannot observe live because USB/IP re-attach outlasts boot.

## Supporting analyses

The long-form working documents behind this summary — the architecture verdict, the
linker/section analysis, the four candidate angles and the four diagnostic lenses —
are in [heap/](heap/), indexed by [heap/README.md](heap/README.md).

## Provenance and confidence

Four independent read-only analyses (agy, codex, opencode, cursor) over separate
lenses — allocation census, policy audit, fragmentation, and budget — plus
device profiling. Their claims were verified before inclusion; several did not
survive. Recorded so the same ground is not re-tilled:

- "Framebuffer is a 48 KB heap allocation" — **wrong**. Static, and 52,272 bytes
  (`MAX_BUFFER_SIZE`), single-buffered.
- "Reduce the `bgwifi` task stack 8192 -> 4096, low risk" — **not low risk**.
  `BackgroundWifiService.h:51` records that route-heavy web handlers run on that
  stack.
- "The 120 KB defrag threshold never clears, so it reboots after every index" —
  observation right, verdict wrong. `EpubReaderActivity.cpp:542-545` says that
  is *deliberate*: "intentionally high so that essentially every long index is
  followed by a refresh". Fair criticism: on a 179 KB heap a 120 KB
  largest-block test is unsatisfiable by construction, so it is written as a
  comparison that can never pass rather than as the unconditional it actually is.
- The ~20 KB gap between linker DRAM and heap pool was initially recorded as
  **unresolved**. It has since been resolved by linker-map analysis: it is TLSF
  allocator bookkeeping plus light poisoning, scaling at ~16 bytes per live
  block, and it is not reclaimable. See the budget section above.

# Contiguity as a managed resource, and reboot-as-defrag

Angle: the ESP32-C3 cannot compact. The device already resets to restore a
virgin TLSF pool. The question is whether that reset should be a *scheduled
layout pass* rather than an exceptional recovery — and whether the 32,768-byte
inflate window should be placed as a long-lived base block on that pass,
instead of being punched out of a working heap at first book open.

Verdict up front: **do not promote reboot to a periodic sweeper.**
Fragmentation here is an event, not a drift. The architectural move is to
treat a *reader-bound reset* as a heap-layout compiler: destination in RTC,
long-lived blocks first, WiFi not at all. That is a property only
`ESP.restart()` can offer on battery hardware. Sleep-wake cannot.

---

## What the heap actually does (so the strategy is aimed at the right thing)

`docs/HEAP_ANALYSIS.md` measured it: largest block collapses in two steps
(boot Home 36,852 → book open 17,396 → first render 9,204) and then **never
moves again**. Page turns are heap-neutral. Post-fix reading plateau is
36,460 free / **15,860 largest**. There is nothing to "wait out" and nothing
for a timer to reclaim.

The 32 KB window is the named split. It is claimed lazily in `Epub::load`
(`lib/Epub/Epub.cpp:469-479` on cache hit, `:521-528` defensively on cache
miss) via `InflateReader::ensureSharedWindow()` (`lib/InflateReader/InflateReader.cpp:65-74`).
Kept for process lifetime (`InflateReader.cpp:11-18`). Freeing it was tried
and reverted: the freed run is 32,756 usable bytes, 12 bytes short of
re-acquisition (`docs/FINDINGS.md` 2026-08-28T12:10Z). TLSF's per-block
header means a block can never hold *itself* again.

ZIP throughput still uses this window. `lib/ZipFile/ZipFile.cpp:10-11,487`
calls `InflateReader::init(true)`. `InflateStream` can borrow the lent
framebuffer (`lib/miniz/src/InflateStream.cpp:25-32`) but that is PNG / loan
scratch, not the EPUB zip path. The pin is real and it happens at book open,
after Home, settings JSON, fonts, and (on a normal open) after the outgoing
activity is still resident (`docs/FINDINGS.md` 2026-08-28T10:45Z).

---

## The two resets are not the same event

### Silent defrag (`ESP.restart`)

`silentRestart()` (`src/main.cpp:129-144`) stamps `RTC_NOINIT_ATTR`
`silentRebootMagic` / `silentRebootTarget` and calls `ESP.restart()`. MCU
stays powered. RTC memory survives. DRAM/heap does not. `setup()` consumes
the magic (`main.cpp:739-745`), skips `BootActivity` (`:747-749`), and
routes to reader or home (`:765-776`).

`esp_reset_reason()` is `ESP_RST_SW`. `HalSystem::isRebootFromCrash()`
(`lib/hal/HalSystem.cpp:189-209`) treats this as Normal, so it does not
land on the crash screen.

### Battery sleep-wake (the common case)

`HalPowerManager::startDeepSleep` drops GPIO13, the battery-latch MOSFET
(`lib/hal/HalPowerManager.cpp:130-149`). The comment is explicit: **the MCU
is completely powered off, including RTC.** On battery this is not
ESP-IDF deep sleep with retained RTC; it is a cold power-on. Wake is
`ESP_RST_POWERON`. `RTC_NOINIT` is garbage. Destination is recovered from
`/.crosspoint/state.json` after SD and settings have already allocated
(`main.cpp:752-793`).

USB-powered sleep can be a real `ESP_RST_DEEPSLEEP` with GPIO wake
(`HalGPIO.cpp:327-346`). That path is not the reading-in-bed path.

### So: is a wake indistinguishable from a defrag reboot?

**Heap: yes. User, clocks, and boot script: no.**

| | Defrag reboot | Battery wake | USB deep-sleep wake |
|---|---|---|---|
| Heap | virgin TLSF | virgin TLSF | virgin TLSF |
| RTC_NOINIT | survives (destination known at `boot:entry`) | dead | may survive |
| Destination known before SD? | yes (`silentRebootTarget`) | no (need `state.json`) | maybe |
| Splash | skipped | `BootActivity` (logo, or `transparent.bmp`) | splash |
| Panel | `display.begin()` still RST+init (`EInkDisplay.cpp:244-291`); `seamless=true` is documented (`HalDisplay.h:20-26`) and **never passed** (`main.cpp:577`) | same, plus splash paint | same |
| `millis()` | 0 | 0 | 0 |
| Reset reason | `ESP_RST_SW` | `ESP_RST_POWERON` | `ESP_RST_DEEPSLEEP` |

The heap equivalence is why reboot-as-defrag works at all. The RTC
difference is why a defrag reboot can be a *better* layout pass than a
wake: it is the only reset on battery that knows "we are going to the
reader" before `SETTINGS.loadFromFile()` has scattered small
`std::string`s across a virgin pool.

---

## What is already persisted, and what a reboot throws away

`persistAndRestartForRecovery` (`EpubReaderActivity.cpp:875-908`) is the
existing contract. It does **not** run `onExit()`.

Persisted, and restored on the next `onEnter`:

- **Page position** — `progress.bin` (spine, page, chapter page-count)
  via `saveProgress` (`:880-882`, format at `:374-392`). Atomic replace
  (`src/activities/reader/ProgressFile.h:12-31`).
- **Open book path** — `APP_STATE.openEpubPath` → `state.json`
  (`src/JsonSettingsIO.cpp:57-69`).
- **Orientation** — `SETTINGS.saveToFile()` (`:885-887`).
- **Section cache** — already on SD; post-reboot path is
  `loadSectionFile`, not `createSectionFile`. This is why the after-index
  reboot cannot boot-loop (`EpubReaderActivity.cpp:545,1896-1897`).
- **Book metadata / CSS caches, bookmarks, annotations, per-book
  settings, pokemon** — on SD under `.crosspoint/epub_<hash>/`.
- **User settings, WiFi credentials, recent books** — on SD, reloaded in
  `setup()`.

Lost (RAM only; `onExit` would have handled some of these, reboot does not):

- **Footnote return stack** — `savedPositions[]` / `footnoteDepth`
  (`EpubReaderActivity.h:139-140`). `onExit` writes the *origin*
  (`EpubReaderActivity.cpp:477-482`). Recovery writes the *current*
  page. A defrag mid-footnote resumes inside the note with no Back-to-origin.
- **Text selection mode** — cursor, overlay, 48 KB snapshot.
- **Auto page-turn** — `automaticPageTurnActive`, `lastPageTurnTime`
  (`:53,46-47`). `millis()` is 0 after reset anyway.
- **`pagesUntilFullRefresh`** — refresh-cycle counter; next paint may
  be a full flash.
- **Reading-stats session RAM** — `beginSession` uses `millis()`
  (`ReadingStatsStore.cpp:117-132`); last deferred save can be up to 30 s
  stale (`DEFERRED_SAVE_INTERVAL_MS`). Sub-minute sessions may not meet
  `MIN_COUNTED_SESSION_MS` (60 s).
- **`pendingPageTurn`, in-RAM alerts** — not in `state.json`.
- **Font glyph cache** — rebuilt on first render. Cost is CPU and a
  cluster of small heap blocks, which is exactly the post-window
  fragmentation of the first-render step.
- **WiFi association** — gone. See the Always-mode trap below.
- **`ImageBlock` session failure set** — `onEnter` already clears it
  (`:331-334`). Harmless.

Resume of *reading* is cheap. Resume of *session chrome* is not. That is
acceptable for a phase transition the user just caused (finished an
index, left WiFi, opened Controls). It is not acceptable as a
page-turn-cadence heartbeat.

---

## Wall-clock and battery cost

Not measured on this pass (read-only). Bounds from existing traces and
hardware behaviour:

- **CPU**: `ESP.restart()` + `setup()` to first reader paint. Silent path
  skips the splash e-ink update. SD init + display RST (~40 ms in
  `resetDisplay`, `EInkDisplay.cpp:298-306`) + cache load + one page
  deserialize. Likely **~1–3 s** to the next `displayBuffer`, dominated
  by the panel, not the ROM.
- **Panel**: one extra refresh of the same page. HEAP_ANALYSIS notes the
  after-index reboot produces a blank captured frame. E-ink refresh is
  600–3700 ms (brief). That is the user-visible cost.
- **Battery**: MCU stays at 160 MHz for those few seconds (~tens of mA).
  One extra full update is comparable to one extra page turn. Sleep is
  *cheaper* than defrag because GPIO13 cuts the rail entirely. Defrag is
  *shorter* than a battery wake (no splash, no power-on settle).
- **USB-CDC**: reset drops the host; FINDINGS 2026-08-14T00:30Z measured
  ~15–20 s before serial reappears. Irrelevant to a reader; fatal to
  anyone trying to watch the reboot with `device_walk`.

The 18 silent restarts in 7 minutes (`docs/FINDINGS.md` 2026-08-12T19:45Z)
is the existence proof that "routine" without a phase gate is visible and
wrong. That run was a CSS-heavy book indexing as it was read: every
foreground `createSectionFile` sets `heapDirtyFromIndexing_`
(`EpubReaderActivity.cpp:1890-1898`), and the 120 KB largest-block test
(`:550-552`) is unsatisfiable on a 179 KB heap (HEAP_ANALYSIS "Fair
criticism"). So after-index reboot is already an unconditional per-chapter
reset on a cold book. Warm cache does not set the flag (`:1899-1900`).

Silent *prefetch* indexing (`performDeferredSilentIndexingLocked`,
`:2117-2151`) does **not** set the flag. It can dirty the plateau without
a recovery. That is a seam, not an argument for more reboots.

---

## The Always-mode trap: today's defrag wastes the clean heap

`setup()` after routing to the reader always calls
`attemptBootAutoConnect()` (`main.cpp:797-798`). That function does **not**
consult `blocksBackgroundServer()` (`BackgroundWifiCoordinator.cpp:175-191`).
In Always mode it starts `BG_WIFI` — the same ~22 KB that
`goToReader` just learned to tear down *before* a document load
(`ActivityManager.cpp:323-336`).

First `loop()` then reconciles: reader returns true from
`blocksBackgroundServer()` (`EpubReaderActivity.h:230`), so
`evaluateReconcile` stops the server (`BackgroundServerPolicy.cpp:76-81`).

Sequence on a silent reboot *to the reader* in the default Always
configuration:

1. Virgin heap (the whole point).
2. Settings / display / fonts allocate small long-lived blocks.
3. `Epub::load` pins the 32 KB window in whatever is left of the largest run.
4. Auto-connect starts the background server (~22 KB, many blocks).
5. `onEnter` / reconcile tears it down. TLSF cannot compact. Holes remain.
6. Reading plateau, window in the middle of the wreckage.

`recoverHeapAfterWifi` is worse when Always is on: it **unconditionally**
`silentRestart()` to **home** (`main.cpp:168-172`, default `target=0`).
Home then auto-connects again. Defrag into the measured worst state
(11.5 KB free / 8 KB largest).

A reboot is only a defrag if the post-reboot script does not immediately
rebuild the fragmentation you just paid to destroy.

---

## Allocation ordering: why "first on a clean boot" is the 32 KB fix

TLSF on a virgin pool is one free run. The first `malloc(32768)` is
taken from the base of that run; everything after is "above" it. Small
frees coalesce with their neighbours; they never have to jump over a
32 KB pin in the middle of the working set.

Today the window is malloc N for large N:

- `SETTINGS.loadFromFile` / ArduinoJson / settings strings
  (`main.cpp:658`)
- `display.begin` / `renderer.begin` (the latter
  `bwBufferChunks.assign` of ~6 pointer slots, `GfxRenderer.cpp:85-96`)
- `BootActivity` glyph cache, on non-silent boots
- Home carousel, on a normal `goToReader` (outgoing activity still live)
- *then* `ensureSharedWindow` inside `Epub::load`

That is how a 36,852-byte largest block at Home becomes a 17,396-byte
largest block at book open: the window *is* the 36 KB run, split, and
the remainder of that run is ~4 KB, so largest falls to whatever the
second-largest hole was.

On a **reader-bound silent reboot**, destination is in RTC at
`boot:entry` (`main.cpp:88-92,609`). The window can be `malloc`'d before
settings JSON. Sleep-wake on battery cannot do this: RTC is off, and
`lastSleepFromReader` is only known after `state.json`.

This is the argument for keeping defrag reboot as a distinct tool rather
than folding it into "wake already resets." Wake resets. Defrag can
*layout*.

Oversize-to-free (`malloc(32768+32)`) would dodge the 12-byte trap if we
ever wanted to release the window. It does not help Home+Always, which
cannot afford to hold 32 KB extra. First-on-reader-boot + never-free-
during-session avoids the trap without paying at Home.

---

## Ranked ideas

### 1. Reader-bound boot as a heap-layout compiler (the structural move)

**Mechanism.** Give a reader-bound reset a different `setup()` script
from a Home boot.

On `silentRebootMagic == SILENT_REBOOT_MAGIC && target == READER`
(available at `boot:entry`):

1. `InflateReader::ensureSharedWindow()` immediately — malloc #1 on the
   virgin pool. Optionally peek `book.bin`'s `hasDeflatedEntries`
   (`BookMetadataCache.h:20-21,151`, `BookMetadataCache.cpp:424-428`)
   after SD mount if we want to skip stored-only books; defaulting to
   always-pin on reader-bound boots is simpler and matches the common
   EPUB.
2. Do not call `attemptBootAutoConnect()` (`main.cpp:797`). Reader
   already blocks the server; starting it to stop it is the Always trap.
3. Skip splash (already done).
4. Then `goToReader` as today. `ensureSharedWindow` is idempotent
   (`InflateReader.cpp:66-67`).
5. Pass `display.begin(true)` (`HalDisplay.h:20-26`) so the documented
   seamless path actually runs. On X4 `requestResync` is already a no-op
   (`EInkDisplay.cpp:222-225`); the hardware RST still happens. Worth
   doing for X3 and for honesty.

Apply the same *WiFi skip* to sleep-wake-into-reader
(`lastSleepFromReader`). The early pin cannot be as early (no RTC on
battery), but it can still happen after `state.json` and before
`Epub::load`, which is earlier than today.

**Number it moves.** The book-open collapse 36,852 → 17,396 (pre-fix)
/ the remaining 15,860 plateau (post-fix). If the window is no longer
carved from the working set, the plateau largest-block should track
"free minus small holes" rather than "second-largest hole after a 32 KB
punch." A realistic target is **~25–32 KB largest while reading** if
free stays ~36 KB, versus 15,860 today. That unlocks JPEG/AA (8 KB
scratch already gated on `kCriticalFloorBytes`) and most image
decoders. It does **not** unlock a 48 KB Controls preview
(`ReaderOptionsMemoryPolicy.h:13-14`): 48 KB largest is impossible while
the book is resident and free is 36 KB. Controls still need a loan, a
degraded layout, or a reboot *that unloads the book*.

**Cost.** Near-zero flash (a branch in `setup()`, one early malloc).
No extra RAM: the 32 KB is paid today, just later. Latency: slightly
*less* than today's silent reboot if WiFi is not started. Complexity:
one boot policy, testable with `CMD:HEAPPROF` / `heaptrace` marks.

**Must be true.** (a) Reader-bound silent reboot is already accepted UX
(it is). (b) Always-mode users will tolerate WiFi staying down while
reading (the reader already claims this via `blocksBackgroundServer`).
(c) TLSF really does hand the first large alloc the base of the pool —
true of ESP-IDF TLSF on a single region. (d) Settings/JSON small allocs
after the pin do not themselves split the remaining run worse than the
window currently does.

**Day experiment.** Firmware patch, reader-bound only, no product
change: in `setup()`, if silent target is reader, `ensureSharedWindow()`
right after `heaptrace::mark("boot:entry")` and skip
`attemptBootAutoConnect`. Open a warm-cache deflated EPUB. Dump
`heaptrace` + `CMD:HEAPPROF` at reading plateau. **Kill** if largest
block moves by less than 8 KB versus the 15,860 baseline. **Prove** if
it lands ≥24 KB. Compare also a battery sleep-wake into the same book
(window allocated later): if silent-reboot plateau is materially
better, that is the RTC-as-layout-plan thesis.

Files: `src/main.cpp` (`setup`, `silentRestart`, `attemptBootAutoConnect`
call), `lib/InflateReader/InflateReader.cpp`, optionally a 4-byte peek
helper on `BookMetadataCache`.

---

### 2. Phase-transition reboots, destination-aware — never to Home+Always

**Mechanism.** Contiguity is scheduled at *phase changes*, not on a
timer.

Keep:

- After foreground `createSectionFile` (`heapDirtyFromIndexing_`,
  `:546-567`). Already the right phase (transients dead, cache on SD).
  Consider actually testing a *reachable* largest-block floor (e.g. 24 KB)
  so a lucky index that left a healthy run does not pay a refresh; the
  120 KB test is an unconditional by construction.
- `MEMORY_RECOVERY_REQUESTED` from Controls/Reader Options
  (`EpubReaderMenuActivity.cpp:133-138,152-161`) when the preview/settings
  alloc cannot fit.

Change:

- `recoverHeapAfterWifi` (`main.cpp:160-190`): if the user is returning
  to a book (`APP_STATE.openEpubPath` set, or caller passes reader
  target), `silentRestartToReader()` not `silentRestart()`. If Always
  mode would just rebuild the server at Home, **do not reboot to Home**;
  either reboot to the next real destination or skip. The Always branch
  (`:168-172`) that always reboots is defrag theatre.
- `goToReader` (`ActivityManager.cpp:311-348`): after stopping servers,
  if `largestBlock() < 32768+margin` and we have a persistable path,
  persist and `silentRestartToReader()` *before* `Epub::load`. This is
  the Home→reader transition: servers are down but the Home activity and
  its leftover holes are still there (FINDINGS 2026-08-28T10:45Z). A
  reboot here is the one that can pin the window as malloc #1.

Do not add:

- Every N page turns. Plateau is flat.
- After silent prefetch (`:2117-2151`) unless a later open of Controls
  fails — prefetch is optional work; rebooting for it punishes reading.

**Number it moves.** Converts the Home→reader min-free of 3,848–4,164
(cold) into a clean-boot load. After-index already does this for
chapters. The new win is the *first* open from Home+Always, and WiFi→reader
without bouncing through Home.

**Cost.** Same as today's silent reboot per transition (one extra
page paint). Complexity: one helper, "persist path + silentRestartToReader
if largest < T", called from two sites. Flash: tens of bytes.

**Must be true.** Persist-before-reset already works (it does, for
index and Controls). Users accept a flash when leaving WiFi or opening a
book (they already accept it after a long index). Always-mode product
intent is "server while at Home", not "server during the reader load."

**Day experiment.** Log `largestBlock` at `goToReader` after the existing
server stop, before `ReaderRegistry::open`. If it is already ≥40 KB on
the post-fix firmware, **kill** the extra Home→reader reboot — the
server teardown is enough and a reset would only cost a paint. If it is
<32,768, that is the missing phase. Separately, Always-mode: silent
reboot to home, HEAPPROF at Home settled; expect ~8 KB largest. That
kills "defrag to Home."

Files: `src/main.cpp` (`recoverHeapAfterWifi`, `setup` auto-connect),
`src/SilentRestart.h`, `src/activities/ActivityManager.cpp`,
`src/activities/reader/EpubReaderActivity.cpp` (threshold).

---

### 3. Make the after-index reboot a *layout* reboot, not a bounce

**Mechanism.** Today's after-index path already persists and
`silentRestartToReader()` (`:559,873-908`). Combine with idea 1 so that
the reboot you already take after every long index is the one that
places the window first and never starts WiFi. Then the 18-restarts
cold-book case at least *converges*: chapter 1 indexes, reboots, pins
window, subsequent chapters load from a heap whose large pin is at the
base. If plateau largest is then enough for `createSectionFile`
transients (framebuffer loan already covers the 48 KB,
`EpubReaderActivity.cpp:1861-1878`), later chapters may not need to
reboot at all.

Lower `kHeapDefragLargestBlockThreshold` from 120 KB to something the
post-layout plateau can pass (e.g. 20–24 KB). Then a successful layout
reboot is also the last reboot.

**Number it moves.** Cold-open reboot count: 1 (or 1 per chapter that
still cannot index) instead of one per chapter. Steady-state largest
block as in idea 1. Indexing itself is not made cheaper — the loan
already exists — but the *follow-up* session is.

**Cost.** Same reboot you already take, once. Changing the threshold is
a one-constant experiment.

**Must be true.** Idea 1 actually raises the plateau above whatever
threshold we pick. If it does not, leaving 120 KB (unconditional) is
honest.

**Day experiment.** After idea 1 is on device: cold-open a multi-chapter
book, count `[MAIN] Silent restart (target=1)`. **Prove** if count is 1
and later chapters load without `heapDirtyFromIndexing_` firing.
**Kill** if it is still one reboot per chapter — then indexing transients,
not the window, are the remaining split, and reboot cannot help without
also changing the indexer (out of this angle).

Files: `EpubReaderActivity.cpp:546-567,1890-1898`, `main.cpp` boot
script.

---

### 4. (Weaker, do not lead with this.) Oversized pinned slot as a
non-reboot alternative for Home↔reader

Allocate `32768+32` once, keep it, *or* free it knowing the hole can hold
it again. This is the 12-byte trap's local patch. It does not raise the
reading plateau; it only makes release/re-acquire possible. Home+Always
still cannot hold the slot. Reboot is the right way to *move* the slot
to the base of the pool; oversizing does not move it. Mentioned so it is
not rediscovered as a competitor to idea 1.

---

## Why the obvious answers are wrong here

**Reboot every N pages / on a timer.** Page turns do not fragment
(HEAP_ANALYSIS table). You would pay a panel refresh to restore a heap
that has not changed. The 18-restarts finding is what this looks like
when the trigger fires too often.

**"Wake already defrags, so just sleep."** Battery wake is a cold power-off
of MCU+RTC (`HalPowerManager.cpp:130-143`). It restores a virgin *pool*,
not a *layout*. Destination is unknown until `state.json`. It also costs
a splash (or a transparent BMP restore) and a full rail cycle. Defrag
reboot is the only reset that can be a layout compiler on battery. Do not
collapse the two.

**Static / BSS inflate window.** Moves 32,768 from heap into `.bss`.
Heap pool shrinks by the same amount. Home+Always is already 11.5 KB
free; it cannot donate 32 KB. The framebuffer is already 52,272 of BSS
(`HEAP_ANALYSIS.md` budget). A permanent window is a Home-killer unless
Always-mode dies as a product choice (out of scope here).

**Free the window on book close.** Measured trap. The hole is 12 bytes
too small. Ruled out by the brief; the mechanism is TLSF headers, not a
bug in `releaseSharedWindow`.

**Lower `kLowFloorBytes` / the 120 KB test as a tune.** The 120 KB test
is intentionally unsatisfiable so every long index reboots. Tuning it
without a layout pass either reboots forever (too high) or skips the
one reboot that could pin the window (too low). The threshold is not
the idea; the boot script after it is.

**Reboot to Home to "get a clean heap" then open the book.** Always mode
rebuilds the server on that Home. You defragmented into the worst state
on the device. `recoverHeapAfterWifi`'s default target is this mistake.

**Rely on `FrameBufferLoan` instead of reboot.** The loan is the right
tool for *indexing* (48 KB scratch while the panel holds the popup). It
cannot hold the inflate window *during reading*: the framebuffer is the
page. Concurrent. Different lifetimes. Do not confuse a build-phase
scratch with a session-lifetime dictionary.

**Make Controls fit by rebooting harder.** `kReserveLargestBlock = 48000`
with ~36 KB free while the book is loaded is arithmetic, not
fragmentation. A perfect layout still cannot produce a 48 KB block from
36 KB free. Controls need to borrow the framebuffer, drop the preview
(already implemented), or unload the book. Reboot does not print memory.

---

## What would have to be true to *schedule* contiguity

A scheduler, not a hope, needs all of these:

1. **A destination known before small allocs** — true for
   `ESP.restart()` via RTC; false for battery wake.
2. **A persist contract that covers the user's place in the book** —
   true (`progress.bin` + section cache + `openEpubPath`).
3. **Lost RAM chrome is OK at that seam** — true after index / WiFi /
   Controls; false mid-footnote, mid-selection, mid-auto-turn. Gate the
   reboot on `footnoteDepth==0 && !selectionMode && !automaticPageTurnActive`,
   or persist the origin as `onExit` already does.
4. **The post-reboot script does not re-fragment** — **false today**
   for Always mode (`attemptBootAutoConnect` after `goToReader`). This is
   the actual blocker. Until it is false, "more reboots" makes Always
   users worse.
5. **The trigger is a phase, not a level** — largest-block-on-plateau
   is a *state*, not a trend. Schedule on Home→reader, WiFi→reader,
   index→read. Do not schedule on "largest < X" inside `loop()` during
   reading; that is the 120 KB test's cold-book pathology.
6. **One reboot converges** — after-index plus layout (ideas 1+3) must
   leave later chapters below the trigger. If indexing itself still
   cannot fit even with a base-pinned window and a framebuffer loan, the
   remaining problem is not contiguity-as-resource, it is peak
   transient usage during parse (a different angle).

Until (4) is fixed, promoting reboot from exceptional to routine is
how you get 18 flashes in 7 minutes *and* a still-fragmented plateau.

---

## Honesty

The architecture is already right that **the only compaction is a
reset**, and the device already has the persist/resume machinery to make
that reset cheap for reading. That is not a new invention; it is
`persistAndRestartForRecovery` and `recoverHeapAfterWifi`.

There is a real structural move in this angle, and it is not "reboot
more." It is: **a reader-bound `ESP.restart()` is the only battery-safe
moment at which contiguity can be *placed*, because the destination lives
in RTC and the pool is empty.** Treat that boot as an allocation plan
(window first, WiFi never). Use it at the three phase changes that
actually destroy the run (leave WiFi, leave Home, finish an index). Do
not use it as a sweeper, do not aim it at Home, and do not expect it to
invent a 48 KB block while 36 KB are free.

If idea 1's day experiment does not move largest-block by 8 KB, then
this angle is exhausted: the remaining splits are the many small live
blocks (font cache, page elements, ~380 allocated), which are a pool/
arena problem, not a reboot problem. Say so and stop.

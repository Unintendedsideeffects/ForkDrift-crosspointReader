# The framebuffer as a memory resource

The 52,272-byte static array at `EInkDisplay.h:38,158` (`frameBuffer0[MAX_BUFFER_SIZE]`) is the largest contiguous RAM on the device. It is not heap. Lending it does not increase `ESP.getFreeHeap()`. It is a *second address space* that a 32–48 KB job can occupy instead of punching a hole in TLSF. That is the only number it can move: **largest free heap block**, by not taking one.

On X4 the *used* length is 48,000 (`HalDisplay::BUFFER_SIZE`, `GfxRenderer::frameBufferSize` after `begin()`). The extra 4,272 bytes are X3 padding. InflateStream asks for ~41,136 (`STATE_ALIGNED + 32768` in `InflateStream.cpp:15,28`). It fits. PNGdec’s `sizeof(PNG)` is 58,912 — it does not.

---

## How the loan actually works

Three layers, one owner:

1. **GfxRenderer** (`GfxRenderer.cpp:98–151`, contract at `GfxRenderer.h:256–294`). `releaseFrameBufferForBuild()` copies the pointer, sets `GfxRenderer::frameBuffer = nullptr`, and calls `buildscratch::lend(scratch, frameBufferSize)`. It does **not** touch `EInkDisplay::frameBuffer`, which still points at `frameBuffer0`. Restore calls `buildscratch::reclaim()`, re-reads `display.getFrameBuffer()`, and `memset`s white. Nested `FrameBufferLoan` is inert (`hasFrameBuffer()` is already false). Restore-with-no-loan is ignored so a live frame is not blanked.

2. **buildscratch** (`BuildScratch.cpp:8–49`). One global block. `claim()` is an atomic CAS; one claimant. `claim(minLen)` hands out the *whole* block or nothing — leftover bytes are dead. `reclaim()` while still claimed logs and then clobbers; the storage is never freed, so the failure mode is corrupt decode, not a dangling pointer.

3. **The only consumer today**: `InflateStream::init` (`InflateStream.cpp:25–36`). If a loan is up, state+window live in the array. If not, two heap `malloc`s. `PngToBmpConverter` is the only production caller of `InflateStream`. `ZipFile` still uses `InflateReader` (`ZipFile.cpp:4,11`) and the process-wide 32,768-byte heap window (`InflateReader.cpp:9–20,65–74`).

Call sites that construct `FrameBufferLoan`:

| Site | Holds RenderLock? | Why it is legal |
|---|---|---|
| `EpubReaderActivity.cpp:1873` foreground `createSectionFile` | Yes — inside `render()`, and `renderTaskLoop` (`ActivityManager.cpp:85–94`) takes the lock before `render()` | Popup already on the panel (`drawPopup` at 1859). Every exit redraws. |
| `TrmnlViewActivity.cpp:125` PNG→BMP repack | Yes — `prepareRenderable()` is only called from `render()` (header contract at `TrmnlViewActivity.h:63–68`) | Whole screen redraws after. |
| `SleepActivity.cpp:254` `repackPngForSleep` | **No** | Safe only because `SleepActivity` has no `render()` and never `requestUpdate()`. Documented at `docs/FINDINGS.md` 2026-08-15T17:30Z. |

Silent next-chapter index (`performDeferredSilentIndexingLocked`, `EpubReaderActivity.cpp:1868–1870,2037–2048,2117`) **deliberately takes no loan**. It runs inside `displayBufferAsync`…`finishDisplayBuffer()`. `finishDisplayBuffer` (`EInkDisplay.cpp:924–942`) re-reads the CPU framebuffer into RED RAM as the differential baseline. Lending would seed RED with decoder garbage.

---

## Safety rules, and what actually enforces them

**Must not draw, display, or store/restore grayscale while lent.** Restore leaves white; the caller must full-redraw.

Enforcement is incomplete and social:

- Guarded (log + skip): `clearScreen` (1617), `displayBufferAsync` / `finishDisplayBuffer` (1656, 1677), `storeBwBuffer` / `restoreBwBuffer` (2201, 2245), `invertRect` (1625), `showLoadingPopupTrampoline` (3166). `clearScreen` is explicit that `EInkDisplay::clearScreen` does *not* null-check (`GfxRenderer.cpp:1614–1616`) and would wipe the borrower.
- **Not guarded:** `drawPixel` (`GfxRenderer.cpp:583–615`) does `target = frameBuffer` and writes through it. A concurrent render is a null deref, not a skip. `invertScreen` (1644) is the same. Fast text paths at 492–502 also take `getFrameBuffer()` with only a `!= nullptr` on the fast path.
- `EInkDisplay::getFrameBuffer()` / `HalDisplay::getFrameBuffer()` still return the live array. The loan is a GfxRenderer fiction. Anyone who goes around GfxRenderer (screenshot at `main.cpp:937`, `EInkDisplay.cpp:1289`) sees borrower bytes.

**The RenderLock is the real mutex.** `FrameBufferLoan` does not take it. The comment at `GfxRenderer.h:279–282` is the contract. Nothing in `lend()` checks it.

### The `ActivityManager.cpp:194` hazard

```
lock.unlock();  // onEnter may acquire its own lock
currentActivity->onEnter();
```

`renderTaskLoop` waits on a notification, then takes `RenderLock`, then calls `currentActivity->render()`. After 194 the mutex is free. A notify already queued (or `requestUpdate()` from `onEnter` itself — `EpubReaderMenuActivity.cpp:115` does this at the end of `onEnter`) lets the render task run **concurrently with `onEnter()`**.

`Activity::render` defaults to empty (`Activity.h:56`), which is why Sleep’s loan survives. Giving Sleep a `render()`, or lending from any `onEnter` that also `requestUpdate()`s, is a timing-dependent null-FB crash. `FINDINGS.md` already called this the second occurrence of the class.

`Epub::load` is worse: it runs in `goToReader` (`ActivityManager.cpp:323–340`) *before* `replaceActivity`, while Home is still `currentActivity`. Home’s render task can fire the whole time. A loan around CSS/OPF/inflate at load time without first taking `RenderLock` is a race with `HomeActivity::render`.

`coverThumbBakeTask` (`EpubReaderActivity.cpp:113–159`) is a third task, priority 0, **no lock**, and is the reason cover thumbs cannot borrow today even though `queueCoverThumbBakeIfIdle` peeks the lock before *starting* the task.

---

## Each named consumer: can it borrow, and what stops it

### CSS parsing — no, not as a 52 KB block; maybe as a sequenced arena

`Epub::parseCssFiles` (`Epub.cpp:368–426`) runs during `Epub::load`, not during the chapter-build loan. The 40 KB floor (`Epub.cpp:27,382`) is **total free**, not largest-block. The live allocations are `std::unordered_map<std::string, CssStyle>` nodes (`CssParser.h:157`), a 1 KB stack buffer (`CssParser.cpp:18`), and a 512-byte read buffer. `loadFromCache` may `reserve` ~6 KB of buckets (`CssParser.cpp:921`). None of that is framebuffer-sized.

`createSectionFile` *does* run under the existing loan and calls `cssParser->loadFromCache()` (`Section.cpp:390–397`). At that moment InflateStream is *not* on the ZIP path, so the 48 KB is often sitting lent and **unclaimed**. CSS still heap-allocates map nodes because nothing in CssParser calls `buildscratch::claim`. Even if it did, `claim()` is exclusive of the whole block — CSS and a 41 KB inflate cannot share.

CSS is a peak-then-`releaseMemory()` (`CssParser.h:124`, `Epub.cpp:450`) pattern. An arena that dies at serialize-to-`css_rules.cache` is the right shape. The framebuffer can be that arena only if (a) load holds `RenderLock`, (b) inflate has already released its claim, (c) rules are written to SD before restore. Today none of those are true.

### 32 KB inflate window — yes during loan-capable phases; ZipFile is not wired

This is the miss. The chapter-build loan was built for InflateStream. Chapter builds inflate **HTML through ZipFile → InflateReader → `g_sharedWindow` on the heap**. The 52 KB is lent and idle. `Epub::load` then calls `InflateReader::ensureSharedWindow()` (`Epub.cpp:477,527`) and keeps 32,768 bytes for the process lifetime. That is the slab that collapses the largest run at book open.

You cannot put that window in the framebuffer *during page composition*: `ImageBlock::decodeToFramebuffer` writes the FB as destination (`ImageBlock.cpp:251–254`). You cannot put it there during `displayBufferAsync` (RED sync). You *can* put it there for load, foreground index (already lent), and silent index *after* `finishDisplayBuffer`.

Freeing the heap window and re-acquiring it is the 12-byte trap already measured. The way around that trap is to **never allocate it on the heap** in those phases.

### Image decode — PNGdec cannot; the streaming path already does, but not in-book

In-book PNG uses PNGdec (`PngToFramebufferConverter.cpp:86–90`), 58,912 bytes, destination = framebuffer. 58,912 > 52,272, and dest conflicts with scratch. JPEG MCU rows are `16 * srcWidth` (`JpegToBmpConverter.cpp:167,656`) — a 1,600 px cover is 25 KB, which would fit leftover scratch, but decode-to-framebuffer has the same dest conflict.

`PngToBmpConverter` + InflateStream already claims the FB and writes a BMP to SD. Sleep and TRMNL use that. In-book images do not: first paint decodes into the FB and streams a pixel cache to SD (`PixelCache.h:64`, `ImageBlock.cpp:241`). Subsequent paints blit the cache (`ImageBlock.cpp:210–212`) and need no decoder.

Cover thumbs (`generateThumbBmps`, `Epub.cpp:844+`) already go through the file converters. The bake task refuses unless free ≥ 96,000 and largest ≥ 64,000 (`EpubReaderActivity.cpp:91–92,145`) — unreachable in every measured state. The decoder would fit in a loan. The task would not: no `RenderLock`.

### 48 KB selection snapshot — structurally cannot

`selectionBaseSnapshot` is a *copy of* the framebuffer so overlay code can restore it (`EpubReaderActivity.cpp:1731–1737,3063–3072`). Using the FB as the snapshot of the FB is a contradiction. `canAllocate(snapshotSize, kLowFloorBytes)` with `kLowFloorBytes = 60 KB` (`HeapGuard.h:46`) refuses in every measured state; the fallback re-renders the page (`selectionSnapshotFallback`). That fallback is the real answer. Region cache (`GfxRenderer.h:313–316`) already exists for ~16 KB dirty rects.

The same contradiction applies to `EpubReaderMenuActivity`’s `savedPageBuffer` (`EpubReaderMenuActivity.cpp:100–114`) and `ReaderOptionsMemoryPolicy::kReserveLargestBlock = 48000` (`ReaderOptionsMemoryPolicy.h:14`). These are attempts to own two framebuffers. The heap cannot. The loan cannot either.

### Cover thumbnails / Home cover buffer — same contradiction for the RAM clone; bake can borrow if it is not a third task

`HomeActivity::storeCoverBuffer` (`HomeActivity.cpp:970–1009`) mallocs `getBufferSize()` so Home can memcpy the composed screen back without redrawing. Destination of the restore *is* the FB (`1012–1024`). Gated by `HomeCoverCachePolicy` at `kLowFloorBytes` after a 48 KB take — correctly never succeeds while reading, and at Home+server (11.5 KB / 8.2 KB) never succeeds either. Carousel already has SD backing (`HomeCarouselCache.cpp:236,271`). The RAM clone is a luxury that should stay dead.

Cover *generation* (decode a JPEG/PNG to a small BMP on SD) does not need a second FB. It needs ~43 KB of inflate/MCU scratch and a lock. Today it is a background task with a 96 KB / 64 KB cargo-cult gate.

### Settings list rebuild — not a contiguous 48 KB problem

`kMinHeapForSettingsRebuild = 48000` (`SettingsActivity.cpp:50,138`) is `ESP.getFreeHeap()`, not `largestBlock()`. `getSettingsList` materializes `std::vector<SettingInfo>` (`SettingsList.h:1079`). `SettingInfo` is ~200 bytes plus five `std::function`s (`SettingsList.h:562`). ~15 KB of many small blocks, which is why `forEachSetting` already exists for the web path (`SettingsList.h:547–555`). Lending 52 KB does not help `std::function` heap closures. The 48,000 constant is leftover “one framebuffer of headroom” thinking, same family as `kMinHeapForSettingsApply` (`main.cpp:1053`) and `kMinHeapForSettingsList` (`SettingsHandlers.cpp:10`).

### XTC page buffer — 1-bit can decode into the FB; 2-bit cannot

`XtcReaderActivity::renderPage` (`XtcReaderActivity.cpp:202–228`) mallocs a second 48,000-byte (1-bit) or 96,000-byte (2-bit) buffer, `loadPage`s into it, then `drawPixel`s into the renderer. 1-bit XTG is row-major packed, same size as the panel FB. A 96 KB 2-bit plane pair does not fit. While that malloc is live they also need the FB as dest, so a *loan* (which nulls GfxRenderer’s pointer) does not help unless `loadPage` writes through `EInkDisplay::getFrameBuffer()` and they skip the blit, or they load into the FB then transform in place.

---

## Ranked ideas

### 1. Make ZIP inflate a BuildScratch consumer and retire the process-wide heap window

**Mechanism.** Switch `ZipFile`’s deflate path (`ZipFile.cpp:10–16,444–613`) from `InflateReader` to `InflateStream`. During any `FrameBufferLoan`, the 32,768-byte dictionary + ~11 KB tinfl state live in `frameBuffer0` and cost the heap nothing. Stop calling `InflateReader::ensureSharedWindow()` from `Epub::load` (`Epub.cpp:477,527`) when a loan is active for the whole load+index.

Take three loans, all under `RenderLock`:

- **Book open.** In `goToReader`, after stopping servers (`ActivityManager.cpp:333–336`), `RenderLock` then `FrameBufferLoan` around `ReaderRegistry::open` / `loadDocumentNoThrow`. Home stays on the panel. Restore whites the CPU buffer; `replaceActivity` redraws the reader.
- **Foreground chapter build.** Already there (`EpubReaderActivity.cpp:1873`). Once ZipFile claims, this loan finally pays rent.
- **Silent next-chapter index.** Move it to *after* `finishDisplayBuffer()` (`EpubReaderActivity.cpp:2049–2051`), then loan. The panel already holds the page; RED is already synced. Restore whites CPU RAM; e-ink keeps showing the page; the next `render()` redraws from `section.bin`. You lose overlapping index with the 300–2000 ms waveform. Indexing is seconds; that overlap was never the long pole.

Pre-extract in-book images to the existing pixel cache during `createSectionFile` (under the same loan, using `PngToBmpConverter` / JPEG-to-file, **not** PNGdec). Page paint then only hits `renderFromCache` (`ImageBlock.cpp:210`). First-page image inflate no longer runs while the FB is the destination.

FontDecompressor stays on `InflateReader` (`InflateStream.h:17–20`). Glyph groups are small one-shots; if the shared window is gone, one-shot uzlib (`init(false)`) allocates no 32 KB ring (`InflateReader.cpp:32–51`).

**Number moved.** Removes a 32,768-byte heap slab that is live for the whole book. Reading largest-block 15,860 should move toward the pre-window run (~36 KB at boot, `docs/HEAP_ANALYSIS.md` state table). Cold-open min-free 4,164 should rise by roughly the same 32 KB minus whatever CSS/layout still allocate — the floor during index is no longer competing with a dictionary. Avoids the 12-byte re-acquire trap: the window is never a heap block.

**Cost.** ZipFile rewrite (uzlib callback vs tinfl fill). Load-time `RenderLock` blocks Home’s render task for the duration of OPF/CSS/ZIP (seconds on a cold book). Silent index starts ~0.3–2 s later. Flash: InflateStream already linked; ZipFile is a small TU. Complexity: medium, mostly sequencing.

**Must be true.** (1) Every deflate during load/index happens under a loan. (2) After index, page turns do not inflate (section cache + image pixel cache). (3) Re-entrant inflate falls back to heap (`claim` fails) — nested ZIP reads must not happen under one stream. (4) `FontDecompressor` never needs the shared window.

**Kill/prove in a day.** On device, cold-open a deflated EPUB with a temporary `FrameBufferLoan` around `createSectionFile` only (already present) and a one-line ZipFile→InflateStream switch. Log `Inflate scratch claim ok` vs `Failed to reserve inflate window`. If ZIP entries inflate from scratch, skip `ensureSharedWindow` and print `largestBlock` after `load()`. If largest-block after open stays ~16 KB, something else is the slab and the idea is wrong. If it jumps by ~32 KB, wire load+silent-index next.

Touches: `lib/ZipFile/ZipFile.cpp`, `lib/InflateReader/InflateReader.cpp`, `lib/Epub/Epub.cpp:468–528`, `src/activities/ActivityManager.cpp:311–340`, `src/activities/reader/EpubReaderActivity.cpp:2037–2151`, `lib/Epub/Epub/blocks/ImageBlock.cpp`, `lib/PngToBmpConverter/PngToBmpConverter.cpp`.

---

### 2. Sequence a CSS bump arena in the same 52 KB, after inflate releases

**Mechanism.** `buildscratch::claim` is all-or-nothing (`BuildScratch.cpp:39–44`). Do not share. Order inside one loan: (1) InflateStream claims, ZIP-extract HTML/CSS to the existing temp files on SD (`Epub.cpp:397`, `Section.cpp:336`), `InflateStream::deinit` releases; (2) CssParser bump-allocates `rulesBySelector_` nodes from the same 52 KB (custom allocate, or a 52 KB `unique_ptr` alias onto the claimed pointer); (3) `saveToCache` (`Epub.cpp:436`); (4) `releaseMemory()`; (5) loan ends, memset white.

That matches the existing lifetime: parse is transient, cache is on SD, `createSectionFile` reloads from cache (`Epub.cpp:481–489`, `Section.cpp:394`). The 40 KB total-free floor (`Epub.cpp:27`) becomes irrelevant for the parse peak.

**Number moved.** Cold-open CSS skip was 45–46 KB free vs 65 KB old floor; retuned to 40 KB. The remaining risk is map-node fragmentation (~1500 rules × string+node, `CssParser.cpp:48,429`). Taking that peak off the heap keeps the TLSF run intact for the layout that follows in the same `createSectionFile`. Does not add 52 KB of free heap. Prevents the 4,164 min-free dip from overlapping CSS growth with a 32 KB dictionary.

**Cost.** A bump allocator or a CssParser “external arena” pointer. `std::unordered_map` with a custom allocator is real C++ work under `-fno-exceptions`. Cheaper variant: keep the map on the heap (idea 1 already removed the 32 KB competitor) and *do not* build a CSS arena. Complexity: high for a full arena, low if you stop at idea 1.

**Must be true.** CSS parse can run after ZIP extract (it already does: extract to `.tmp.css`, then `loadFromStream`). Arena is dumped before restore. Selectors fit in 48 KB (MAX_RULES=1500, keys ≤256 bytes but typical keys are tiny; if a pathological sheet overflows, fall back to heap and skip cache, which `parseCssFiles` already does).

**Kill/prove in a day.** Instrument `CssParser::processRuleBlockWithStyle` to count `rulesBySelector_` bytes (key sizes + `sizeof(CssStyle)` + 32). If the working set is < 40 KB on the two ugliest books you have, an arena fits. If it is > 48 KB, kill the arena and keep idea 1 only.

Touches: `lib/Epub/Epub/css/CssParser.cpp`, `lib/Epub/Epub.cpp:368–450`, `lib/Memory/BuildScratch.h` (optional sub-claim API — not required if sequenced).

---

### 3. Unique-framebuffer rule: never malloc a twin; XTC 1-bit loads in place

**Mechanism.** HeapGuard already says it (`HeapGuard.h:44–45`): a 48 KB heap allocation cannot succeed in any measured state. Encode that as an invariant, not a floor:

- Selection: delete the snapshot path; keep `selectionSnapshotFallback` (re-render) or `copyRegionToBuffer` for the highlight union. The 48 KB `canAllocate(..., kLowFloorBytes)` is dead code on hardware.
- Home cover / carousel RAM slots: SD is already the backing store (`HomeCarouselCache.cpp:208–271`). Stop calling `malloc(frameBufferSize)`.
- Menu preview / `ReaderOptionsMemoryPolicy::kReserveLargestBlock`: drop the 48,000 largest-block test; it can never pass after boot. Full-screen settings layouts already handle a null preview (`EpubReaderMenuActivity.cpp:103`).
- Settings 48,000 total-free gates: replace with `heapguard::canAllocate` on the actual ~15 KB vector, or use `forEachSetting` on device too.
- XTC 1-bit: `loadPage` into `display.getFrameBuffer()` (the EInk pointer, which a loan would still leave valid — or don’t loan; this is the dest). Skip `ScopedBuffer`. Handle orientation by setting native landscape or rotating in place. 2-bit stays “memory error” or plane-at-a-time into the 48 KB.

This is not borrowing. It is refusing to pretend a second 48 KB exists.

**Number moved.** Does not add free heap (those mallocs already fail). Removes the 48,000 cargo-cult that blocks settings apply/rebuild (`FINDINGS.md` 2026-08-28T07:20Z) and cover-thumb bake (96 KB / 64 KB). XTC 1-bit becomes renderable: today it needs 48,000 contiguous against a measured max ~37 KB (`HEAP_ANALYSIS.md:211`, `XtcReaderActivity.h:42–45`).

**Cost.** XTC 1-bit in-place needs a layout check (row-major packed vs oriented FB). Selection without snapshot is slower overlay motion (already the fallback). Complexity: low.

**Must be true.** XTG 1-bit bit order matches the panel (MSB-first, `GfxRenderer.cpp:609`) or is cheap to invert. 2-bit XTH is accepted as a degraded format on this chip.

**Kill/prove in a day.** Hex-dump one XTG page vs `renderer.getFrameBuffer()` after a simulator blit. If they match at native landscape, `loadPage(fb)` + `displayBuffer` is the whole patch. For settings, log `sizeof(std::vector<SettingInfo>)` after `getSettingsList` on device — if it is ~15 KB, change the 48,000 gate and see Settings open from Home+server.

Touches: `src/activities/reader/XtcReaderActivity.cpp:202–228`, `src/activities/reader/EpubReaderActivity.cpp:1731–1743`, `src/activities/home/HomeActivity.cpp:970–1009`, `src/activities/settings/SettingsActivity.cpp:50,138`, `src/activities/reader/ReaderOptionsMemoryPolicy.h`, `src/main.cpp:1053`.

---

### 4. (Do not do.) Loan during `displayBufferAsync` or park the 32 KB window in the FB at all times

After `MASTER_ACTIVATION` the *panel* has the bits (`EInkDisplay.cpp:1185–1187`). `displayBufferAsync` is different: `finishDisplayBuffer` SPI-copies the CPU buffer into RED (`EInkDisplay.cpp:936–941`). The FB is busy for the whole wait. Parking the inflate window in the FB between page turns means every `render()` must evict it first; `drawPixel` is unguarded; one missed restore is a black screen or a crash. That is the SleepActivity invariant, applied to the hottest path.

---

## Why the obvious answers are wrong here

**“Just loan it whenever the panel is not refreshing.”** Refresh is not the constraint. `displayBufferAsync`’s RED resync is. Silent index is already in the only overlap window, and that window forbids touching the FB. Between page turns the FB holds the *next* differential baseline. Whitening it is fine only if the next paint is a full redraw and nothing displays in between.

**“Use the FB as the selection snapshot / cover cache / menu preview.”** Those exist to be a copy *of* the FB while the FB is being drawn on. A loan nulls the draw pointer. There is one 48 KB array. You cannot snapshot it into itself.

**“Put PNGdec in the framebuffer.”** 58,912 > 52,272, and PNGdec’s draw callback writes the FB (`PngToFramebufferConverter.cpp:114–169`). Sleep/TRMNL already learned this and switched to `PngToBmpConverter`. In-book PNG has to take that same detour (idea 1’s pre-extract), not a bigger loan.

**“Free the 32 KB window on book close / between chapters.”** Measured: the freed run is 12 bytes too small to hold the window again. Any free-and-reacquire of a max-sized block on this allocator loses. Idea 1 never puts it on the heap.

**“Lower the 48,000 settings / cover-thumb gates and the mallocs will succeed.”** `canAllocate(48000)` fails `bytes <= largestBlock()` in every measured state (`HeapGuard.cpp:68–73`, largest 8–16 KB). The gates are not what is stopping those features. There is no 48 KB heap block to find.

**“Loan from `onEnter` / the cover-thumb task / `Epub::load`.”** `ActivityManager.cpp:194` drops `RenderLock` before `onEnter`. `goToReader` loads while Home can still render. The bake task has no lock. The loan’s safety is “hold `RenderLock` or have no `render()`.” That is not optional.

**“Sub-allocate leftover 7–11 KB after InflateStream claims.”** `claim()` is exclusive. Leftover is unused. A sub-claim API is extra mechanism for a remainder smaller than CSS’s map. Sequence instead (idea 2).

---

## Plain verdict

The loan mechanism is the right primitive. It is under-used in one specific way: **the 32 KB dictionary that actually needs a large run does not consume it**, and the phases that could lend (load, silent index after RED sync) do not. That is a wiring and sequencing bug dressed up as a memory shortage.

There is no move that turns the framebuffer into general-purpose heap, no way to clone it, and no way to decode PNGdec or 2-bit XTC into it. After ZipFile claims scratch and the 48,000 cargo-cult gates die, what remains is incremental: CSS arena only if the rule working set fits; XTC 1-bit in-place; selection/cover staying on re-render and SD.

The architecture of “one static FB, lend it as scratch when you are not allowed to draw, persist results to SD” is already the shape this hardware wants. Finish that shape. Do not invent a second one.

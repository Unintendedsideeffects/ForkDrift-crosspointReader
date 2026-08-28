# Verdict: ship C4 as a transactional STORED shadow, using C2 only for conversion

## Executive decision

Ship **C4**, but do not rewrite the user's EPUB in place. Build a validated, disposable STORED shadow archive beside the firmware's derived data, retain the original EPUB as the source of truth, and use the shadow for subsequent reads. Use C2's `InflateStream` plus a framebuffer loan only as the one-time conversion engine.

Do not ship C2's broader proposal—routing every ZIP inflate through a framebuffer loan—as the first fix. Its premise understates the number of ZIP/PNG call sites and the locking obligation at each one. C4 concentrates the hazardous operation in one pre-open transition; after that, the existing STORED read path needs neither a dictionary nor a loan.

Run the zero-firmware STORED-twin experiment before implementation. If it does not materially improve the largest free block under controlled warm-cache, clean-reboot conditions, stop: C4 has been falsified cheaply.

## 1. Adjudication of the load-bearing claims

### Summary

| Candidate | Claim | Verdict |
|---|---|---|
| C2 | ZIP inflate never uses the framebuffer loan; `InflateStream` is the sole `buildscratch::claim` consumer; only Sleep/TRMNL PNG repack calls it | **Half-true** |
| C2 | `FrameBufferLoan` does not lock; drawing while lent can null-dereference; `onEnter()` is outside `RenderLock` | **True, with a scheduling qualifier** |
| C2 | `PNGdec` is 58,912 bytes, larger than the framebuffer | **True** |
| C2 | The Settings 48,000 gate is total-free cargo cult; the setting list is about 15 KB of small nodes | **Half-true** |
| C4 | `ZipFile` already supports STORED without a dictionary | **True** |
| C4 | `hasAnyDeflated()`/cached flag guarantees a STORED book never claims the 32 KB window | **Half-true** |
| C4 | Conversion can run under the existing loan via `InflateStream` | **Half-true: feasible, not implemented or intrinsically safe** |
| C3 | Boot enters the reader, then starts the server, and the first loop tears it down | **Half-true** |
| C1 | `shared_ptr` costs two extra blocks per line and refcounts never exceed one | **Half-true** |

### C2: ZIP inflate versus the framebuffer loan — half-true

The important first clause is true. `ZipFile` includes and constructs `InflateReader`, not `InflateStream`, at `lib/ZipFile/ZipFile.cpp:4-16`, `lib/ZipFile/ZipFile.cpp:473-500`, and `lib/ZipFile/ZipFile.cpp:558-627`. `InflateReader` owns the process-lifetime shared 32 KiB window at `lib/InflateReader/InflateReader.cpp:11-20` and `lib/InflateReader/InflateReader.cpp:65-74`; it has no connection to `FrameBufferLoan`.

The second clause is also true in the current tree: the production call to `buildscratch::claim()` is in `InflateStream::init()` at `lib/miniz/src/InflateStream.cpp:20-35`, with release at `lib/miniz/src/InflateStream.cpp:69-78`.

The exclusivity claim is false. `PngDecodeContext` embeds `InflateStream` at `lib/PngToBmpConverter/PngToBmpConverter.cpp:177-204`, and production PNG conversion is called not only by Sleep/TRMNL but also by EPUB cover generation at `lib/Epub/Epub.cpp:790-810`, EPUB thumbnail work at `lib/Epub/Epub.cpp:844-934`, and Pokémon sprite caching at `src/util/PokemonSpriteCache.cpp:51-66`. Sleep and TRMNL are the two obvious places that pair the converter with an explicit framebuffer loan (`src/activities/boot_sleep/SleepActivity.cpp:251-266` and `src/features/terminus_sleep/TrmnlViewActivity.cpp:121-132`), but they are not the only `InflateStream` call paths. EPUB also loans the framebuffer around section work at `src/activities/reader/EpubReaderActivity.cpp:1861-1878`, while its ZIP decompression still goes through `InflateReader`.

That distinction matters: “only consumer of the scratch arena” is not the same as “only caller that can execute while the framebuffer is lent.”

### C2: `RenderLock` and loan concurrency — true, with a scheduling qualifier

The API contract explicitly requires the caller to hold `RenderLock` for the full loan lifetime at `lib/GfxRenderer/GfxRenderer.h:274-282`. The loan constructor itself only lends the buffer at `lib/GfxRenderer/GfxRenderer.cpp:134-140`; release nulls the renderer's framebuffer pointer at `lib/GfxRenderer/GfxRenderer.cpp:98-112`.

The renderer task independently acquires `RenderLock` before rendering at `src/activities/ActivityManager.cpp:85-94`. Activity transition changes the activity while locked, releases the lock at `src/activities/ActivityManager.cpp:196`, and calls `onEnter()` at `src/activities/ActivityManager.cpp:201`. Therefore a loan created by `onEnter()` has no mutual exclusion against the render task.

The failure mode is real: `drawPixel()` obtains the current framebuffer pointer at `lib/GfxRenderer/GfxRenderer.cpp:581-590` and writes through it at `lib/GfxRenderer/GfxRenderer.cpp:607-615`. While lent, that pointer is null.

The qualifier is that a race is **permitted**, not guaranteed on every `onEnter()`. A deferred `requestUpdate(false)` may not render until after a synchronous `onEnter()` returns, but an already queued render notification, immediate update, or another drawing path can overlap. The design cannot rely on timing.

### C2: `PNGdec` size — true

The current measured `sizeof(PNG)` is 58,912 bytes (`docs/FINDINGS.md:1465-1473`). The X4 backing array is 52,272 bytes at `open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h:33-38` and `open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h:158`. The X4 renderer intentionally exposes only 48,000 bytes as loanable build scratch at `lib/hal/HalDisplay.h:28-32`.

Thus `PNGdec` exceeds the physical array by 6,640 bytes and the usable loan by 10,912 bytes. The older 59,456-byte number in `docs/FINDINGS.md:456-466` is stale; the load-bearing conclusion is unchanged.

### C2: Settings 48,000 gate and allocation shape — half-true

There is no 48 KB settings allocation. `SettingsActivity::onEnter()` compares `ESP.getFreeHeap()` to 48,000 at `src/activities/settings/SettingsActivity.cpp:133-147`, so this is a total-free check, not a largest-block check. Moreover, that particular gate only controls `sdFontSystem.refreshIfDirty()`; the cached settings list is built afterward regardless at `src/activities/settings/SettingsActivity.cpp:146-147`. The web and serial settings paths also materialize settings through `src/network/server/SettingsHandlers.cpp:7-18`, `src/network/server/SettingsHandlers.cpp:40-50`, and `src/main.cpp:1048-1066` without that activity gate.

The “about 15 KB” estimate for the base list is supported by the source comment and sizing model at `src/SettingsList.h:540-563`. But “small nodes” is misleading. `getSettingsList()` counts entries and reserves one `std::vector<SettingInfo>` backing array at `src/SettingsList.h:1074-1100`; that base is a relatively large contiguous allocation. Each `SettingInfo` also contains nested vectors, strings, and five `std::function` objects at `src/SettingInfo.h:43-87`, which add smaller allocations.

So the allocator diagnosis is directionally right—total free does not prove allocability—but the exact gate does not protect the list, and the list is not merely 15 KB of independent small nodes. “Cargo cult” is an interpretation, not a fact established by the code.

### C4: STORED read path — true

Method 0 is explicitly recognized at `lib/ZipFile/ZipFile.cpp:19`. A whole STORED entry is copied directly at `lib/ZipFile/ZipFile.cpp:462-471`, and a STORED stream is copied in chunks at `lib/ZipFile/ZipFile.cpp:529-555`. Neither path initializes `InflateReader` or needs the 32 KiB dictionary.

### C4: deflate detection guarantees no shared window — half-true

`ZipFile::hasAnyDeflated()` scans central-directory methods at `lib/ZipFile/ZipFile.cpp:382-429`. `BookMetadataCache` calculates and persists the flag at `lib/Epub/Epub/BookMetadataCache.cpp:124-154` and restores it at `lib/Epub/Epub/BookMetadataCache.cpp:421-429`. On a metadata-cache hit, `Epub::load()` conditionally calls `ensureSharedWindow()` at `lib/Epub/Epub.cpp:467-480`. The host test covering the warm STORED case is at `test/host/test_conditional_inflate_window.cpp:153-158`.

But the cache-miss path still calls `ensureSharedWindow()` unconditionally at `lib/Epub/Epub.cpp:517-529`. A STORED book opened without a valid `book.bin` can therefore allocate and pin the window. A `book.bin` created from the original deflated archive can also carry a stale `hasDeflatedEntries=true` after an out-of-band conversion. Finally, once `g_sharedWindow` has been allocated, it is retained for the process lifetime at `lib/InflateReader/InflateReader.cpp:11-20`; opening a STORED book later does not recover those bytes.

The optimization works on the intended warm-cache path. It is not a universal guarantee.

### C4: conversion under the framebuffer loan — half-true

It is mechanically viable. `InflateStream` claims its state and dictionary from build scratch at `lib/miniz/src/InflateStream.cpp:20-35`; the measured claim is 41,136 bytes at `docs/FINDINGS.md:1623-1625`, which fits in the 48,000-byte X4 loan.

It is not an existing conversion path. `ZipFile` still uses `InflateReader`, and there is no STORED ZIP writer. `InflateStream` also falls back to heap allocation when scratch claim fails at `lib/miniz/src/InflateStream.cpp:34-50`, which would reintroduce exactly the fragmentation/reacquisition failure this design is meant to remove. The converter therefore needs a strict “scratch required” mode.

Nor does “can use a loan” imply thread safety. Conversion must run in one controlled, locked pre-open phase. A loan from `onEnter()` is unsafe for the reasons above.

### C3: reader boot, autoconnect, and first-loop teardown — half-true

The ordering is real. `setup()` routes to the reader through `goToReader()` at `src/main.cpp:765-793`, then calls `attemptBootAutoConnect()` at `src/main.cpp:795-798`. The boot-autoconnect path does not first consult the active activity's server-blocking policy (`src/network/background/BackgroundWifiCoordinator.cpp:175-191`); with usable credentials it starts background Wi-Fi at `src/network/background/BackgroundWifiCoordinator.cpp:97-105`. Starting the service creates its task, whose stack is 8,192 bytes at `src/network/background/BackgroundWifiService.h:52` and whose startup path is at `src/network/background/BackgroundWifiService.cpp:321-370`.

The first loop reconciles policy at `src/main.cpp:1113-1116`. EPUB declares that it blocks background serving at `src/activities/reader/EpubReaderActivity.h:225-230`; the policy chooses stop at `src/network/background/BackgroundServerPolicy.cpp:69-81`, and the coordinator executes that stop at `src/network/background/BackgroundWifiCoordinator.cpp:111-117`.

What is overstated is “starts the ~22 KB server.” The measured full web-server footprint is about 22 KB (`docs/HEAP_ANALYSIS.md:153-157`), but the HTTP server/routes are allocated only after Wi-Fi connects at `src/network/background/BackgroundWifiService.cpp:131-207`. Depending on timing, the first policy reconciliation can stop the service before that full graph exists. C3 identifies a real, low-risk boot-order bug and should be fixed separately, but it does not prove the full 22 KB is always allocated and freed in that interval.

### C1: `shared_ptr` allocation and reference counts — half-true

`Page` stores `PageLine` elements as `std::shared_ptr` at `lib/Epub/Epub/Page.h:123-127`; `PageLine` owns a `shared_ptr<TextBlock>` at `lib/Epub/Epub/Page.h:33-39`.

On the normal cached deserialization path, `TextBlock::deserialize()` allocates a separate object as `unique_ptr` at `lib/Epub/Epub/blocks/TextBlock.cpp:284-291`. `Page::deserialize()` converts it to `shared_ptr` and then separately converts/allocates a `PageLine` at `lib/Epub/Epub/Page.cpp:61-68` and `lib/Epub/Epub/Page.cpp:440-445`. Because those objects were not created with `make_shared`, their control blocks are separate allocations. That is indeed two control-block heap allocations for a line with its text block, in addition to the two object allocations.

It is not true for every construction path. Live parsing constructs `PageLine` with `make_shared` at `lib/Epub/Epub/ChapterHtmlSlimParser.cpp:2150-2152`, combining that object and control block. It is also false that reference counts never exceed one: the line-builder callback accepts/copies `shared_ptr<TextBlock>` at `lib/Epub/Epub/ParsedText.h:51-55`, `lib/Epub/Epub/ParsedText.h:93-95`, and `lib/Epub/Epub/ChapterHtmlSlimParser.cpp:2118-2152`; table layout has another copy path at `lib/Epub/Epub/ChapterHtmlSlimParser.cpp:534-538`. The sharing appears mostly transient rather than semantically necessary, but “never” is refuted.

C1 points at real allocator churn, especially during cached deserialization. It is a broader ownership refactor whose heap benefit has not yet been isolated by measurement.

## 2. Why C4 is the shipment candidate

The measured failure is about **contiguity**, not merely aggregate bytes. The current instrumentation correctly reports both total free and largest free block (`src/main.cpp:946-966`), and the 12-byte re-acquisition trap is documented at `docs/FINDINGS.md:1847-1852`: after a 32,768-byte allocation is released, adjacent allocator metadata can leave a 32,756-byte hole that can never reacquire it. Both C2 and C4 avoid repeated heap ownership of that exact window.

The choice is operational:

| Candidate | Benefit | First-shipment risk |
|---|---|---|
| C1 | May reduce many small allocations and fragmentation | Broad ownership/serialization refactor; benefit unquantified; does not remove the steady 32 KiB pin |
| C2 global | Removes heap dictionary while loan is held | Every ZIP inflate context must acquire a safe loan; missed background/Home/KOReader paths either fail or fall back to heap |
| C3 | Avoids pointless boot-time service churn | Low-risk companion fix, but allocation-order gains are timing-dependent and it leaves the dictionary in heap |
| C4 shadow | Removes dictionary from all steady reads using the existing method-0 path | Adds a one-time converter and SD-derived-data protocol, but localizes risk and is reversibly bypassed |

C2's runtime blast radius is larger than its report acknowledges. Home thumbnail generation loads/extracts EPUB data at `src/activities/home/HomeActivity.cpp:466-483`; the reader background thumbnail task at `src/activities/reader/EpubReaderActivity.cpp:126-151` does not hold `RenderLock`; KOReader invokes ZIP-backed mapping at `src/activities/reader/KOReaderSyncActivity.cpp:147-162`, with spine streaming at `lib/KOReaderSync/ProgressMapper.cpp:700-702`; EPUB entry streaming is spread across `lib/Epub/Epub.cpp:81-404` and `lib/Epub/Epub/Section.cpp:319`. Proving a global loan protocol at all of those sites is a larger first change than it looks.

C4 and C2 are complementary, but asymmetrically:

- C4 owns the steady-state representation and read path.
- C2 supplies a strict, temporary conversion engine at one audited transition.
- The converter must not have a heap fallback.
- The original book remains untouched, so disabling the feature or booting the prior OTA image restores old behavior immediately.

This is less elegant than making every inflate borrow display memory. It is safer to ship because the concurrency proof is made once, persistent user data is not mutated, and rollback does not depend on the converter having been perfect.

## 3. Concrete design, in dependency order

### Gate 0 — falsify C4 before changing firmware

Create byte-equivalent twins of one representative EPUB: the original deflated archive and a `zip -0` STORED archive. Preserve EPUB requirements: `mimetype` must be the first entry, uncompressed, and contain exactly `application/epub+zip`; preserve entry names and relative paths.

The current cold path unconditionally allocates the window, so the experiment must be run as follows for **each** twin:

1. Install only that twin under a distinct path.
2. Open it once to build `book.bin`.
3. Reboot, because `g_sharedWindow` is process-lifetime once allocated.
4. Open the already-cached twin and run the same labeled reader walk.
5. Reboot before testing the other twin.

Confirm the warm STORED run logs “not needed for this book” at `lib/Epub/Epub.cpp:473-479`, while the deflated run logs the reserve. Use at least five rebooted runs per twin and compare median and worst-case largest block. A first-open or same-boot comparison is invalid.

### Step 1 — make storage capacity and commit status observable

Touch, in order:

1. `open-x4-sdk/libs/storage/SDCardManager.h` and its implementation: expose free bytes from the mounted FAT volume and a result-bearing sync/flush operation.
2. `lib/hal/HalStorage.h` and `lib/hal/HalStorage.cpp`: add `freeBytes()`, propagate sync success/failure, and expose a stable source fingerprint input. `HalFile` already exposes 64-bit size and FAT modification time at `lib/hal/HalStorage.h:78-98`, while current `flush()` returns no status and merely forwards at `lib/hal/HalStorage.cpp:232`.
3. Simulator and host-test storage implementations/mocks: implement the same interface and support injected short writes, failed sync, and rename failure.

Before conversion, compute the exact STORED output size from the central directory: sum of uncompressed payloads, local headers/names/extras, central records, and EOCD. Require that amount plus temporary marker space and a conservative filesystem reserve. Failure to determine capacity is a safe refusal, not permission to start.

### Step 2 — add a strict framebuffer-backed inflate mode

Touch `lib/miniz/src/InflateStream.h` and `lib/miniz/src/InflateStream.cpp`.

Add an explicit backing policy, for example `ScratchPolicy::RequireBuildScratch`. In that mode, failure of `buildscratch::claim()` must return an error; it must not execute the heap fallback currently at `lib/miniz/src/InflateStream.cpp:34-50`. Keep the existing fallback behavior for current PNG callers until they are independently migrated.

The converter creates and destroys one `InflateStream` per deflated entry, verifies its output byte count and CRC, and releases scratch before proceeding. A log/metric must distinguish “scratch claimed” from “heap fallback”; the latter is a converter-fatal invariant violation.

### Step 3 — inventory, write, and validate a STORED ZIP

Touch `lib/ZipFile/ZipFile.h` and `lib/ZipFile/ZipFile.cpp`.

Add:

- A tri-state-or-richer inventory result: `StoredOnly`, `HasDeflate`, `Unsupported`, or `Corrupt`. A boolean cannot safely distinguish “no deflate” from “inspection failed.”
- Central-directory inventory containing entry order, names, flags, method, CRC, compressed/uncompressed sizes, timestamps, attributes, offsets, and calculated output size.
- A streaming method-0 writer. Method-0 inputs copy directly; method-8 inputs go through strict `InflateStream`.
- Output CRC and byte-count verification per entry.
- A full reopen validator for local headers, central directory, EOCD, counts, offsets, methods, sizes, and CRCs.

For the first version, reject encryption, ZIP64, multi-disk archives, methods other than 0/8, impossible offsets/counts, and malformed names. Do not enable miniz's general archive APIs; they are disabled by configuration at `lib/miniz/src/MinizConfig.h:8-13`, and widening that library surface is unnecessary.

Preserve source entry order and EPUB's `mimetype` rule. Because sizes and CRCs are known before output begins, write complete local headers and clear the data-descriptor flag in the derived archive rather than reproducing descriptors.

### Step 4 — own derived archives transactionally

Add a focused component such as:

- `lib/Epub/Epub/StoredEpubCache.h`
- `lib/Epub/Epub/StoredEpubCache.cpp`

Store shadows outside the existing per-book render-cache directory, for example:

```text
/.crosspoint/stored-epub/<path-hash>.tmp
/.crosspoint/stored-epub/<path-hash>.epub
/.crosspoint/stored-epub/<path-hash>.ready
```

That separation matters because `Epub::clearRenderCache()` deletes non-user-state files in its render-cache directory at `lib/Epub/Epub.cpp:638-677`.

The readiness marker contains a schema version, canonical original path, original size, FAT modification timestamp, a source central-directory checksum/count, output size, output entry count, and output central-directory checksum. Size/mtime makes the cheap check fast; central-directory identity prevents a same-size/same-timestamp replacement from silently reusing the wrong shadow.

Use this commit protocol:

1. Remove or truncate only a stale derived `.tmp`; never touch the original.
2. Check projected output size and free-space reserve.
3. Write every header and payload to `.tmp`, checking each return value.
4. Flush/sync and close `.tmp`.
5. Reopen `.tmp` and validate the complete ZIP, entry count, byte counts, methods, and CRCs.
6. Rename `.tmp` to the final derived `.epub`.
7. Write a temporary readiness marker, sync it, and rename it to `.ready`.
8. Select the shadow only when marker, source fingerprint, and a lightweight shadow validation all agree.

FAT rename should not be treated as a transactional database primitive. The marker is the commit record. Power loss before the marker leaves either `.tmp` or an uncommitted final; both are ignored on the next open and can be removed/rebuilt. Power loss after marker commit leaves a validated shadow. At every point the original EPUB remains readable.

### Step 5 — separate book identity from the archive read path

Touch `lib/Epub/Epub.h`, `lib/Epub/Epub.cpp`, and, where necessary, `lib/Epub/Epub/BookMetadataCache.cpp`.

Keep `filepath` as the original user-visible identity. It continues to determine the render-cache hash, progress/annotation state, recent-book identity, upload replacement, and KOReader mapping identity. Add an internal `archivePath` that resolves to the validated shadow or falls back to `filepath`.

Route ZIP opens through `archivePath`, including the current sites at `lib/Epub/Epub.cpp:335`, `lib/Epub/Epub.cpp:963`, `lib/Epub/Epub.cpp:980`, and `lib/Epub/Epub.cpp:985`, and pass the effective archive path into metadata extraction at `lib/Epub/Epub.cpp:597`. Audit all `ZipFile(filepath)` construction in `Epub`/`Section`; do not change `getPath()` to return the shadow.

Replace the unconditional cache-miss reservation at `lib/Epub/Epub.cpp:517-529` with the live inventory of `archivePath`:

- `StoredOnly`: never call `ensureSharedWindow()`, even while rebuilding `book.bin`.
- `HasDeflate`: use the legacy reserve/fallback path.
- `Unsupported` or `Corrupt`: fail with a precise reason or fall back conservatively to the original; never interpret inspection failure as STORED.

Do not trust a cached `hasDeflatedEntries` value after selecting a different effective archive. Recompute it from `archivePath` and persist the corrected value when rebuilding metadata.

### Step 6 — perform conversion in one safe pre-open phase

The narrowest EPUB-specific entry is `features::epub::createActivity()` at `src/features/epub/Registration.cpp:23-36`. It runs after `ActivityManager::goToReader()` has stopped background services at `src/activities/ActivityManager.cpp:323-338`, and before `core::loadDocumentNoThrow<Epub>()` allocates/parses the book. Put the lazy `StoredEpubCache::prepare(path)` call there, before line 30.

The exact lock/loan sequence is:

1. Construct `RenderLock` on the task performing `createActivity()`.
2. While locked and before lending memory, render and synchronously display a simple “Preparing book” screen.
3. Enter a nested scope and construct `GfxRenderer::FrameBufferLoan`.
4. Run only the strict streaming converter; do not call `requestUpdate()`, take screenshots, or invoke activity callbacks while the loan exists.
5. Destroy every `InflateStream`, then destroy `FrameBufferLoan` at the end of the nested scope.
6. Release `RenderLock` only after the loan has restored the framebuffer.
7. Load the resulting STORED archive; the reader's first update performs a full redraw.

In pseudocode, the lifetime relation must be structural:

```cpp
RenderLock renderLock;
drawAndDisplayPreparingScreen(renderer);
{
    GfxRenderer::FrameBufferLoan loan(renderer);  // lock is already held
    prepareStoredShadowStrict(path, loan.data(), loan.size());
}  // framebuffer restored first
// renderLock released later
```

Do **not** move this to `onEnter()`: `ActivityManager` deliberately releases `RenderLock` before `onEnter()` at `src/activities/ActivityManager.cpp:193-201`.

Make the existing social contract executable in debug builds. Add current-task ownership tracking to `RenderLock` and assert in `FrameBufferLoan` construction/release that the current task owns the render mutex; `RenderLock::peek()` at `src/activities/RenderLock.h:16` only reports availability and cannot prove ownership. Do not make `FrameBufferLoan` acquire the mutex itself: existing call sites that already hold the mutex would deadlock. Audit and update the three existing loan sites so the assertion is satisfied.

If preparation fails because of capacity, unsupported ZIP features, CRC mismatch, I/O failure, or failure to claim scratch, delete/ignore derived state and load the untouched original through the current `InflateReader` path. Conversion is an optimization; it must not strand a book.

Yield or reset the watchdog between bounded chunks, but keep `RenderLock` for the complete loan lifetime. The e-paper panel already displays the preparation screen, so holding the CPU framebuffer does not blank the visible UI.

### Step 7 — invalidate and migrate lazily

Existing books are handled on first open, not by a boot-time card scan:

- Original is already `StoredOnly`: use it directly and skip shadow creation.
- Matching validated shadow exists: select it.
- Original fingerprint changed: remove the derived marker/shadow and reconvert lazily.
- Deflated original plus sufficient space: convert once.
- Unsupported archive, insufficient space, or failed conversion: record a diagnostic and use the original legacy path.

Extend the EPUB upload/replacement hook at `src/core/features/FeatureModules.cpp:440-445` to invalidate the matching derived archive. Manual SD replacement is caught by the source fingerprint. Never background-convert the entire library at boot; that creates boot latency, write amplification, and a card-capacity surprise unrelated to the book the user chose.

After successful conversion on firmware that may already have allocated `g_sharedWindow`, offer or schedule one controlled reboot. The shadow prevents future allocation, but only reboot can return a previously pinned process-lifetime window immediately.

### Step 8 — preserve a real field rollback

The partition table has two OTA application slots at `partitions.csv:4-5`. Firmware switching writes the new OTA selection through `src/network/ota/FirmwareFlasher.cpp:362-365` and `src/network/ota/OtaBootSwitch.cpp:57-82`. There is no repository call to `esp_ota_mark_app_valid_cancel_rollback()` or equivalent health-confirmation flow, so two slots must not be described as automatic rollback. Operational rollback is selecting/flashing the prior slot.

The shadow design makes that useful:

- The previous firmware reads the untouched original and ignores the new derived directory.
- Progress, annotations, and recent-book identity remain keyed to the original.
- New firmware can ship a kill switch that disables shadow selection without deleting user data.
- Derived files can be garbage-collected later, after the rollout is proven.

Keep the original EPUB at least through the full rollout and one rollback window; the safer default is to retain it permanently. An in-place conversion would make a converter defect survive firmware rollback even though STORED is theoretically readable by old code.

## 4. Verification plan

### Host tests for the operator to run

Extend `test/host/test_conditional_inflate_window.cpp` and add focused `StoredEpubCache`/ZIP writer tests using `test/host/zip_deflate_fixtures.h`:

- STORED, deflated, and mixed archives.
- UTF-8 names, extras, empty entries, data descriptors, and EPUB `mimetype` ordering.
- Rejection of encryption, ZIP64, unsupported methods, bad offsets, bad CRC, and truncated inputs.
- Output reopen, byte-for-byte uncompressed payload comparison, CRC/count/offset validation.
- Strict scratch mode refuses heap fallback.
- Failure injection after every header/payload/flush/rename/marker step; reboot recovery always opens the original or a fully validated shadow.
- Source fingerprint changes invalidate the shadow.
- A cache miss for a validated STORED shadow never calls `ensureSharedWindow()`.

No build was run during this adjudication; the operator owns compilation and execution.

### Device heap walks

`CMD:HEAPPROF` currently reports timestamp, free, largest, minimum-ever free, total heap, free-block count, and allocated-block count at `src/main.cpp:946-966`. The `heap <label>` verb is declared at `scripts/device_walk.py:27-35` and writes those fields to `heap.csv` at `scripts/device_walk.py:212-245`.

Use clean rebooted, matched walks for the deflated original and validated STORED shadow. At minimum record:

```text
heap boot
heap home
heap before_open
heap convert_before
heap convert_loaned
heap convert_after
heap reader_loaded
heap first_page
heap turn_1
heap turn_5
heap reader_steady
heap settings_open
heap return_home
heap reopen_reader
```

`convert_*` labels apply to the first migration run; the steady comparison must happen after a reboot. Test at least five rebooted trials per variant with the same book, cache state, settings, font, Wi-Fi state, battery/power source, and page-turn sequence. Compare:

- median and worst-case `largest`, not only `free`;
- `min` through open and conversion;
- `free_blocks` and `alloc_blocks` growth across repeated page turns/reopens;
- `total` as an instrumentation sanity check.

Acceptance evidence should include:

1. No converter log ever reports heap fallback.
2. A cold STORED-shadow metadata rebuild does not allocate the shared window.
3. After reboot, steady reading gains close to 32 KiB total free and materially improves largest block—expected order of magnitude 20–30 KiB, not necessarily exactly 32 KiB because allocator topology differs.
4. Largest block does not erode across repeated open/page/settings/close cycles.
5. Covers, inline images, CSS, KOReader mapping, thumbnails, and font changes still work.
6. SD-full, write-failure, corrupt-input, and unsupported-ZIP cases open the original.
7. Power interruption at every conversion phase leaves the original readable on reboot.
8. Booting/selecting the prior OTA slot after conversion still opens the original with intact progress.

C3's boot-policy fix can be verified independently with `heap after_reader_route`, `heap after_autoconnect`, and `heap first_loop`: when the selected reader blocks serving, `attemptBootAutoConnect()` should be skipped rather than starting and immediately stopping the background task. It is a worthwhile companion patch, not a condition for C4's representation result.

## 5. What would make this verdict wrong

The headline falsifier is a controlled twin measurement:

> After warm-cache, clean-reboot, matched walks confirm that the deflated run allocated the shared window and the STORED run did not, the STORED run improves steady-reading largest free block by **less than 16 KiB**.

That result—especially if total free still rises by roughly 32 KiB—would prove the dictionary consumes bytes but is not the dominant contiguity pin. C1-style page/CSS/object allocation topology would then deserve measurement before adding a conversion pipeline.

Also reject shipment if any representative conversion needs the heap fallback, drives minimum free below the current safe operating margin, cannot fit routinely on users' cards, takes operationally unacceptable time, or fails the power-cut/old-slot rollback matrix. Those results would make global C2 or a narrower allocator/ownership change preferable despite C4's steady-state advantage.

## Bottom line

C4 is the lowest-risk route to removing the 32 KiB process-lifetime pin **provided the STORED-twin test first proves a largest-block gain**. Implement it as derived, transactional, lazily migrated data; preserve the original; fix the cold-cache window gate; and use C2 only inside one strict `RenderLock`-protected conversion phase. C3 is a small independent boot-policy cleanup. C1 remains a plausible second-stage fragmentation optimization, not the first shipment.

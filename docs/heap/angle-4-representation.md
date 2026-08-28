# C4 — Change the on-disk representation so the 32 KB inflate window is never needed

## Verdict

This is a real architectural move, not a threshold tune. The 32,768-byte uzlib dictionary exists only because EPUB entries are DEFLATE-compressed ZIP members (`lib/InflateReader/InflateReader.cpp:9`, `ZIP_METHOD_DEFLATE=8` at `lib/ZipFile/ZipFile.cpp:20`). The device already almost does not need it while reading: the section cache stores laid-out words and image paths on SD, and page turns are heap-neutral. The window is held anyway because the original ZIP remains the durable source of chapter bytes, `Epub::load` pre-reserves it whenever `hasDeflatedEntries` is true (`lib/Epub/Epub.cpp:468-479`), and freeing it is proven not to work (FINDINGS 2026-08-28T12:10Z: the hole is 12 bytes too small to ever hold the window again).

The move: on first open, decompress the book onto SD into a form the existing STORED path can read, using the framebuffer loan so the dictionary never lands on the heap. After that, `hasAnyDeflated()` is false, `ensureSharedWindow()` is never called, and the ~9–16 KB largest-block plateau caused by carving 32 KB out of the biggest run is gone for every subsequent open of that book.

The remaining wins after this are incremental. This one is not.

---

## What is already on SD (and what is thrown away)

Per-book directory: `/.crosspoint/epub_<hash>/` (`Epub.h:49-50`, `src/main.cpp:195`). Durable vs throwaway:

| Entry | When written | Survives? | Needs ZIP inflate to produce? |
|---|---|---|---|
| `book.bin` | first `Epub::load` cache miss | yes (until format bump / re-upload) | yes (container.xml, content.opf, NCX/nav) |
| `css_rules.cache` | `parseCssFiles` | yes | yes (stylesheets streamed, then `.tmp.css` deleted at `Epub.cpp:425`) |
| `sections/<n>.bin` | `Section::createSectionFile` | yes, until layout settings change | yes (chapter XHTML) |
| `.tmp_<spine>.html` | same, `Section.cpp:293,315-319` | **deleted** at `Section.cpp:428` | yes — this is the smoking gun |
| `img_<spine>_<n>.jpg/.png` | first layout of that chapter, `ChapterHtmlSlimParser.cpp:975-984` | yes | yes |
| `img_*.pxc` | first successful decode, `ImageBlock.cpp:209-241` | yes | no (pixels, not ZIP) |
| `cover.bmp`, `thumb_*.bmp` | first cover/thumb bake | yes | yes (cover bytes streamed, temp `.cover.jpg/.png` deleted at `Epub.cpp:780,814,951`) |
| `progress.bin`, `annotations.bin`, `book_settings.json`, `pokemon.json` | reader | yes, kept across `clearRenderCache` (`BookCacheEntries.h:18-23`) | no |

Steady-state page turn, fully indexed chapter: `Section::loadSectionFile` (`Section.cpp:181-265`) deserialises words and styles from `section.bin` (`TextBlock.cpp:161-163`). `ImageBlock::render` hits `.pxc` then the extracted `img_*` file (`ImageBlock.cpp:209-221`) — a plain SD file, never the ZIP. Cover display uses the BMP. CSS is not held in RAM; `createSectionFile` reloads `css_rules.cache` only when laying out (`Section.cpp:390-397`). **Inflate is not on the turn path.** Measured: page turns do not move free/largest (`docs/HEAP_ANALYSIS.md`).

The ZIP is still opened on a warm load only because `Epub::load` treats `hasDeflatedEntries` as "this process will need the window sometime" and claims it up front (`Epub.cpp:468-479`). Cache-miss is worse: it reserves *defensively* before it even knows (`Epub.cpp:521-528`).

What still inflates after a warm, fully-indexed open:

1. A chapter not yet in `sections/` — streams XHTML to `.tmp_*.html` (`Section.cpp:319`).
2. A layout-settings change — invalidates `section.bin`, re-extracts HTML, **and re-extracts images** (parser has no "already on disk" check, `ChapterHtmlSlimParser.cpp:977-984`).
3. KOReader XPath mapping — `streamSpine` → `readItemContentsToStream` (`lib/KOReaderSync/ProgressMapper.cpp:700-702`).
4. CSS cache miss / `book.bin` rebuild / missing cover BMP.
5. WebDAV/OPDS re-upload — `clearRenderCache` then the next open is a cache miss (`CrossPointWebServer.cpp:89-96`, `OpdsBookBrowserActivity.cpp:417-419`).

So: most *reading* never needs inflate. Most *sessions* still allocate the window, because the ZIP is the only copy of the HTML after the temp file is deleted.

---

## Why the obvious answers are wrong here

**Free the window on book close.** Tried (`InflateReader::releaseSharedWindow`, `InflateReader.cpp:57-63`). The freed run is 32,756 usable bytes — 12 bytes short of 32,768 — so the hole cannot hold the window again. Next open fails `ensureSharedWindow`, cannot read `META-INF/container.xml`, cascades into empty CSS. Reverted. Any "convert then free in the same process and later open a still-deflated book" design re-hits this unless conversion used the framebuffer (window never on the heap) or a reboot happened in between.

**Just stop pre-reserving; lazy-allocate at first inflate.** The reservation exists because lazy acquisition fails after the heap is fragmented (`Epub.cpp:521-525`). Indexing already fragments. This is how books became unable to extract figures.

**Rely on the section cache and never convert.** True for page turns. False the moment the user opens an unindexed chapter, changes font size, syncs KOReader, or the CSS cache is invalid. And `Epub::load` still reserves today even when none of those will happen.

**One-shot inflate without a dictionary.** `InflateReader::init(false)` uses the output buffer as the back-reference space (`InflateReader.h:16-19`, `InflateReader.cpp:102-107`). Valid only when the *entire* uncompressed entry fits in RAM. `ZipFile::readFileToMemory` caps at 4 MB (`ZipFile.cpp:449`) and still uses `init(true)` for every deflated member (`ZipFile.cpp:487`). Chapter XHTML is streamed to SD precisely because it does not fit. Conversion must stream; streaming needs a 32 KB window *somewhere*. The somewhere should be the lent framebuffer, not the heap.

**A smaller DEFLATE window.** RFC 1951 window is 32 KB. Publishers encode with that. We do not control the encoder.

**Turn on miniz's ZIP writer and rewrite the archive with the library.** `MINIZ_NO_ARCHIVE_APIS` and `MINIZ_NO_ARCHIVE_WRITING_APIS` are set (`lib/miniz/src/MinizConfig.h:10-11`). Flash is 91.9% full (~500 KB headroom). Enabling the archive layer is a non-starter. A 200-line STORED-only writer that emits local headers + payload + central directory + EOCD is the whole ZIP format we need. CRC32 is already in-tree (`uzlib_crc32` / `crosspoint_mz_crc32`).

**Explode every zip member into a nested directory tree, including unused fonts/audio.** Cluster tax on FAT (often 32 KB/cluster on large cards) times a 2,000-entry O'Reilly book is tens of MB of slack, plus `mkdir` of deep paths under SdFat long-name limits (`ZipFile.h:106` already truncates names at 255). The reader never opens those entries. Convert the working set, or emit one STORED zip.

**In-place grow of the original `.epub`.** STORED members are larger. You cannot expand a ZIP in the middle. Write a sidecar, fsync, then `rename` over the original (or leave the sidecar as the reader's file and keep the original as backup until the next reboot).

**PNG/JPEG "also need 32 KB, so converting the ZIP is pointless."** Different dictionaries. PNG IDAT is zlib inside the image; `PngToFramebufferConverter` uses PNGdec (32 KB *inside* `sizeof(PNG)`, `PngToFramebufferConverter.cpp:81-87`), and `PngToBmpConverter` uses `InflateStream`, which already claims the framebuffer loan (`InflateStream.cpp:25-33`). JPEG is DCT, not DEFLATE. Once `img_*` and `.pxc` exist, page render never inflates ZIP *or* PNG. Converting the ZIP does not shrink PNGdec; it removes the *process-lifetime heap* window that pins largest-block during *text* reading.

**Convert during the OPDS download.** `HttpDownloader::downloadToFile` writes the bytes as received (`OpdsBookBrowserActivity.cpp:406-413`). WiFi and the download buffer are up; this is a bad heap. `ActivityManager::goToReader` already stops both background servers *before* `ReaderRegistry::open` (`ActivityManager.cpp:333-338`). Conversion belongs there, on first open, with a FrameBufferLoan, not on the radio path.

---

## Idea 1 (do this) — Rewrite the book as a STORED zip on first open, under a framebuffer loan, then never claim the window

### Mechanism

ZIP method 0 is already implemented and needs no dictionary (`ZipFile.cpp:462-471` memory, `529-555` stream). `ZipFile::hasAnyDeflated` already walks the central directory and returns false when every member is STORED (`ZipFile.cpp:382-429`). `BookMetadataCache` already persists that as `hasDeflatedEntries` (`BookMetadataCache.cpp:124-154`, `book.bin` v10). `Epub::load` already skips `ensureSharedWindow` when the flag is false (`Epub.cpp:473-475`).

A stored-only EPUB is a valid EPUB: the spec requires only that `mimetype` be STORED and first (already true of every book; see `test/host/zip_deflate_fixtures.h:2-4`). Other members *may* be STORED.

Conversion protocol, first open of a deflated book:

1. Servers already down (`ActivityManager.cpp:333-336`). Draw "preparing book" on the panel.
2. Take `GfxRenderer::FrameBufferLoan` (`GfxRenderer.cpp:134-139`) — e-ink holds the popup without the framebuffer, same contract as chapter indexing (`EpubReaderActivity.cpp:1861-1878`).
3. Stream each ZIP member to a sidecar (e.g. `Book.epub.stored`) with method 0. Copy already-STORED members as-is. Inflate method-8 members with **`InflateStream`**, not `InflateReader`. `InflateStream::init(true)` already does `buildscratch::claim` of state+window into the lent 52 KB framebuffer (`InflateStream.cpp:25-33`, ~11 KB tinfl + 32 KB ring). **The 32 KB dictionary never touches the heap.** Yield (`vTaskDelay`) per member so the watchdog does not fire.
4. Write central directory + EOCD. Fsync. `rename` sidecar over the original path so `BookCachePath` / progress / annotations keep the same hash (`Epub.h:49-50`).
5. Drop the loan (framebuffer restored and wiped white, `GfxRenderer.cpp:122-131`). Either continue into the existing `book.bin` build, or set `heapDirtyFromIndexing_` and let the silent defrag restart (`EpubReaderActivity.cpp:547-568`, `persistAndRestartForRecovery` at `:875`) compact whatever small convert buffers remain.
6. Subsequent `Epub::load`: `hasAnyDeflated()==false` → no `ensureSharedWindow`. Forever, for this file, including font-size rebuilds and KOReader.

Do **not** enable miniz archive APIs. A STORED-only writer is local header (30+name), payload, central dir (46+name), EOCD (22). CRC32 while copying.

`InflateStream.h:12-13` claims tinfl "replaces the uzlib-backed InflateReader on the throughput paths (EPUB zip entries, PNG IDAT)". That is not true today: `ZipFile.cpp:11,487,578` still uses uzlib and the process-lifetime heap window. Conversion should be the first caller that makes the comment true. Even if we never emit a STORED zip, routing `ZipFile::readFileToStream` through `InflateStream` during an already-active chapter-index loan would stop the window being carved out of the heap *during indexing* — but `Epub::load` still pre-reserves before that loan exists (`Epub.cpp:468-479`), so without the representation change the window is still claimed at open. The two changes compose: Stream for the one-time convert, STORED so the convert never runs again.

### Number it moves

- **Largest contiguous block while reading.** Today the window is allocated early from the largest run (`InflateReader.h:64-68`: Terminus one-shot dropped largest 40,948 → 9,204). Reading plateau is 9–16 KB largest with ~36 KB free (brief; `HEAP_ANALYSIS.md` 9,204 after first render). Never allocating 32,768+header (~32,884) means that split never happens. Expected: free +~33 KB (36 → ~69 KB) and largest in the 40–50 KB band rather than 9–16 KB — enough that the 48,000-byte floors which currently fail the largest-block test become reachable during reading.
- **Live heap during reading.** One fewer ~32 KB block; ~16 bytes less TLSF header. Not the block-*count* problem (~380 blocks); the *shape* problem.
- **Does not move flash occupancy** if the writer stays tiny. Does not move the static 52 KB framebuffer.

### Cost

| Axis | Cost |
|---|---|
| RAM at convert | ~2 KB stream buffers (`ZipFile.cpp:558-570` pattern). Window in framebuffer. Heap shape of a chapter index, not of a 32 KB pin. |
| RAM after convert | −32,884 bytes vs today. |
| Flash | ~1–2 KB for a STORED writer. Headroom ~500 KB. Do not link miniz archive. |
| SD, typical novel (1–3 MB EPUB, mostly XHTML) | ~2.5–4× file size → **3–12 MB**. Dual occupancy during convert (original + sidecar), then original gone. |
| SD, image-heavy (10–40 MB, mostly JPEG/PNG) | DEFLATE saves little on already-compressed bytes → **~1.05–1.3×**, often a few MB. |
| SD, already fully indexed | `.crosspoint` (section.bin + img_* + .pxc + BMPs) is often *larger* than this expansion. The expansion is cheaper than the cache we already accept. |
| Latency, first open | Full-book stream inflate on C3 @ 160 MHz. tinfl is "several times faster" than uzlib (`InflateStream.h:13-15`). Ballpark tens of seconds for a novel, minutes for a 30 MB textbook. Must show a popup and yield. Later opens: *faster* (memcpy vs inflate). |
| Complexity | One writer, one convert path, a sidecar/rename protocol, a failure path that leaves the original untouched. Read path is already written. |

### What would have to be true

- Framebuffer loan is available at convert time. True on first reader open after the indexing popup (`EpubReaderActivity.cpp:1861-1864`). Not true during `Epub::load` itself (no activity, no loan yet). Convert must run *after* a screen is up, or `Epub::load` must be split: parse `book.bin` without reserving, then convert-if-needed from the activity with a loan. Cache-miss `Epub::load` currently inflates OPF/CSS *before* any loan (`Epub.cpp:517-551`). **That is the sequencing bug to design around:** either (a) convert the zip as a dedicated step in `goToReader` / `onEnter` before `Epub::load` rebuilds metadata, using a loan, or (b) make `Epub::load` on a deflated file with no `book.bin` *not* parse yet — only convert. Option (a) is cleaner.
- SD free space ≥ uncompressed size + original until rename. Cheap to know *without inflating*: sum `uncompressedSize` from the central directory (`ZipFile.cpp:339-341`, `hasAnyDeflated` already walks it). Fail convert with a message rather than filling the card.
- No zip64, no encryption, no method other than 0/8. Already true of `ZipFile` (`ZipFile.cpp:504,630`). Refuse convert, keep current window behaviour for those (vanishingly few EPUBs).
- Power loss: sidecar incomplete → delete sidecar, original intact. Never rename until EOCD is on disk.
- In-place replace keeps the path, so cache hash, WebDAV, file browser, KOReader sidecars all stay valid.
- `clearRenderCache` on re-upload (`CrossPointWebServer.cpp:94`) must also drop a convert-complete marker if we add one; replacing the `.epub` with a new deflated file is the trigger, and `hasAnyDeflated()` on next open re-converts.

### Cheap experiment (kill or prove in a day)

**Host, no firmware (1 hour, kill/prove SD cost):** Python walk of the central directory of a 9-book library (the same set `test_conditional_inflate_window.cpp:128` already sampled). For each book print `file_size`, `sum(uncompressedSize)`, `sum(uncompressedSize)/file_size`, count of method 8 vs 0, and `sum(uncompressed) - file_size` extra SD. If typical extra is >100 MB, kill for image-heavy libraries or convert only XHTML/CSS/XML (idea 2). If extra is a few MB, the cost argument is settled.

**Host unit test (2 hours):** STORED-writer round-trip of `kZipWithDeflate` (`zip_deflate_fixtures.h`) → `hasAnyDeflated()==false`, `readFileToStream` matches inflated bytes. No device.

**Device, one book, no writer yet (afternoon, prove the heap number):** Take any already-STORED EPUB (Calibre "do not compress" / `zip -0`), or unzip+rezip stored on a PC, copy to SD. Open on device. Confirm log `Inflate window: not needed for this book` (`Epub.cpp:474-475`) and `CMD:HEAPPROF` largest-block vs a deflated twin of the same title. If largest does not jump by ~20–30 KB, the window is not the pin and this whole angle is wrong. If it does, the representation change is the largest-block fix.

**Device, convert under loan (if the first three pass):** smallest possible writer, one book, FrameBufferLoan, rename, reboot, confirm `g_sharedWindow` never allocated (no `ensureSharedWindow` log) and reading largest-block matches the stored-twin measurement.

---

## Idea 2 — Working-set overlay: extract what the reader actually opens, then never open the ZIP again

### Mechanism

Do not rewrite the user's `.epub`. On first open (same loan + `InflateStream` as idea 1), extract only the members the firmware ever reads:

- `META-INF/container.xml`, OPF, NCX/nav
- CSS listed in the OPF plus `discoverCssFilesFromZip` (`Epub.cpp:331-351`)
- every spine XHTML (`getSpineItem(i).href`)
- every image the OPF manifest names with a jpeg/png type (or sniff), plus the cover href
- skip embedded fonts, audio, video, unused extras

Write them under `/.crosspoint/epub_<hash>/src/` with ZIP-relative paths. Marker file `src.complete`. Point `Epub::readItemContentsToStream` / `readItemContentsToBytes` / `getItemSize` at `src/` when the marker exists (`Epub.cpp:955-985`). After that the ZIP handle is never opened. `ensureSharedWindow` is skipped when the marker exists, *even if the original `.epub` is still deflated*.

This is the incremental form of idea 1: it makes durable the files we already extract and then delete (HTML at `Section.cpp:428`, CSS at `Epub.cpp:425`, cover temps at `Epub.cpp:780`). Images are already kept (`ChapterHtmlSlimParser.cpp:975`).

Optional last step: delete or ignore the original `.epub` after `src.complete` to reclaim SD. I would not: WebDAV, the file browser, and Calibre-on-the-card all show the `.epub`. Keep it as the user's file; the overlay is a cache. `clearRenderCache` currently deletes everything except user-state names (`Epub.cpp:638-677`) — **`src/` would be wiped on re-upload unless listed or kept as derived-from-book and rebuilt**. That is correct for a changed book and a trap if someone calls `clearRenderCache` expecting only `sections/`.

### Number it moves

Same RAM as idea 1 once `src.complete` exists (no window). SD: uncompressed working set only. For a novel that *is* almost the whole book. For a 40 MB textbook with 30 MB of unused high-res extras, this can be **much** smaller than a full STORED zip. Conversion time scales with working-set size, not archive size.

### Cost

- More code in `Epub::readItemContentsToStream` (overlay vs zip), path normalisation, `mkdir` of nested `src/` (SdFat, long names).
- FAT cluster waste per file. A 200-chapter novel with 4 KB HTML files on 32 KB clusters is ~6 MB of slack. A 2,000-file O'Reilly book is the reason not to explode the *entire* zip (idea 1's single file wins there).
- Two sources of truth until overlay is complete. Partial overlay still needs the window for missing members — so conversion must be all-or-nothing (same as idea 1's sidecar) or we reintroduce lazy allocation.
- Original deflated `.epub` remains, so library size on disk is original + overlay. Fine if SD is plentiful; worse than idea 1's replace.

### What would have to be true

- Overlay is complete before we skip the window. Same sequencing as idea 1: convert under loan *before* metadata rebuild, or rebuild from overlay.
- KOReader, cover bake, CSS reparse, section rebuild all go through `readItemContentsToStream` — they do, so they pick up the overlay for free.
- `clearRenderCache` / re-upload must delete `src/` (derived). Next open reconverts.
- Deep paths and 255-char names: already a ZipFile limit (`ZipFile.h:106-127`). Overlay cannot be more complete than the zip enumerator.

### Cheap experiment

Same central-directory walk as idea 1, but split `sum(uncompressed)` into (a) XHTML/CSS/XML/OPF/NCX and (b) images and (c) other. If (c) is huge, idea 2 wins on SD. If (c) is noise, idea 1 is simpler and the overlay is wasted complexity.

On device: persist `.tmp_<spine>.html` instead of deleting it (`Section.cpp:428`), skip ZIP on the next `createSectionFile` if the file exists. One-line experiment for *one* chapter. If a font-size change then indexes without `ensureSharedWindow` having been needed *for that chapter*, the "stop deleting HTML" half is proven. It does not yet remove the window for OPF/CSS/other chapters — that is why this is idea 2, not a one-line fix.

---

## Idea 3 — Do not convert the book; stop pre-reserving once the caches that reading actually uses are present

### Mechanism

Not a new container. Change the admission policy:

- If `book.bin`, `css_rules.cache`, and `sections/<current>.bin` all exist, **do not** call `ensureSharedWindow` at `Epub::load` (`Epub.cpp:473-479`).
- On `createSectionFile` / CSS reparse / cover miss / KOReader, take a FrameBufferLoan (indexing already does, `EpubReaderActivity.cpp:1873`) and inflate with `InflateStream` into that loan. Never allocate `g_sharedWindow`.
- Keep deleting temp HTML, keep the deflated ZIP.

This is the "section cache already means most reading never inflates" idea taken literally.

### Number it moves

Warm, fully-indexed open: same as idea 1 (no 32 KB pin). Cold open / new chapter / settings change: window lives in the framebuffer for the duration of the inflate, then gone — **no 12-byte hole**, because it was never a heap block. Reading largest-block should look like idea 1 *until* the user does something that inflates. If they change font size, the index loan already exists; the bug today is that `g_sharedWindow` was claimed at load, hours earlier.

### Cost

- Tiny flash, zero extra SD, zero convert latency.
- Does not help `Epub::load` cache-miss: OPF/CSS inflate happens *before* any loan (`Epub.cpp:517-551`, `ActivityManager.cpp:323-338` loads the document then constructs the activity). Must split load vs. first paint, or loan the framebuffer from `goToReader` before `ReaderRegistry::open`. That lifecycle split is real work (FINDINGS 2026-08-28T10:45Z: outgoing Home activity still resident during factory).
- First visit to chapter N of an unindexed book still inflates (acceptable; indexing already loans FB).
- KOReader sync from the reader menu may not hold a loan; would need one.
- The ZIP remains the source of truth, so we are one forgotten `init(true)` on uzlib away from claiming `g_sharedWindow` again.

### What would have to be true

- Every inflate site either (a) runs under an existing FrameBufferLoan, or (b) is small enough for `init(false)` one-shot into a heap buffer that fits (container.xml, small CSS). `ZipFile.cpp:487` must stop unconditionally calling `init(true)`.
- `Epub::load` must not reserve "in case". The cache-miss defensive reserve (`Epub.cpp:521-528`) is the opposite policy.
- Re-entrant inflate (shared window busy, `InflateReader.cpp:45-48`) must not malloc a second 32 KB. Serial device; if we never have `g_sharedWindow`, re-entry is a bug.

### Cheap experiment

On device, comment out `ensureSharedWindow` at `Epub.cpp:477` only (cache-hit path). Open a fully-indexed deflated book. If reading works and largest-block jumps, the reservation — not inflate-during-turns — is the pin. Then change font size: if indexing fails without the window, idea 3 needs the InflateStream+loan half. If indexing succeeds because the loan is already active *and* ZipFile is switched to InflateStream, idea 3 is enough and ideas 1–2 are SD we do not have to spend.

I would still do idea 1 if this experiment passes for warm opens and fails for rebuilds: rebuilds are common (font size, margins, orientation) and idea 3 makes every rebuild re-solve the dictionary placement. Idea 1 makes rebuilds STORED memcpy.

---

## Ranking

1. **STORED zip rewrite (idea 1).** Smallest read-path change (already written). Valid EPUB. WebDAV/OPDS/file browser unchanged. One extra SD multiple that is bounded and knowable from the central directory before we start. Window never on the heap if convert uses `InflateStream`+loan. After rename, `hasAnyDeflated` stays false across settings changes and KOReader. This is the representation the brief asked for.
2. **Working-set overlay (idea 2).** Prefer if experiment 1 shows huge unused extras, or if replacing the user's `.epub` is politically unacceptable. More code, FAT tax, two sources of truth.
3. **Stop reserving + InflateStream on the existing loan (idea 3).** Cheapest if the only thing we care about is warm indexed reading. Leaves every rebuild and every cache miss as a dictionary-placement problem. Worth the one-line cache-hit experiment *first*, because it proves the number; not the thing I would ship as the architecture.

Do not do: explode the entire zip; enable miniz writers; free `g_sharedWindow` in-process; convert on the OPDS radio path; in-place grow.

---

## What breaks (and what does not)

| Path | Idea 1 (STORED replace) | Idea 2 (overlay) | Idea 3 (policy only) |
|---|---|---|---|
| Page turns / `loadSectionFile` | unchanged | unchanged | unchanged |
| Font/margin/orientation rebuild | STORED memcpy, no window | overlay memcpy, no window | inflate under loan, no heap window if Stream-migrated |
| Cover / thumbs | stream from STORED; BMP cache still wins | from `src/` or BMP cache | ZIP inflate on miss |
| Image extract during layout | from STORED; `img_*` still written | from `src/`; skip ZIP if we also skip re-extract | ZIP inflate every rebuild (parser always extracts) |
| Metadata rebuild | STORED OPF | overlay OPF | ZIP inflate |
| OPDS download | write deflated; convert on first open | same | no convert |
| WebDAV/USB replace | `clearRenderCache`; next open reconverts | delete `src/`; reconvert | unchanged |
| KOReader XPath | STORED stream | overlay stream | ZIP inflate |
| Home cover thumb bake | `Epub::load(..., skipCss)` still must not reserve if STORED / overlay complete | same | if `book.bin` says deflated, still reserves today |
| Encrypted / zip64 / method≠0,8 | refuse convert; old path | same | old path |
| Power loss mid-convert | original intact | partial `src/` discarded | n/a |
| Flash size | +1–2 KB writer | +overlay path logic | ~0 if Stream already linked |
| User-visible `.epub` size | larger | unchanged | unchanged |

PNG first-decode and PNGdec's internal 32 KB are **out of scope** of a ZIP representation change. They are already gated (`PngToFramebufferConverter.cpp:89-97`) and after `.pxc` exists they are idle.

---

## Does the section cache already mean most reading never needs inflate?

**Yes, for turns. No, for the process.** `section.bin` holds the words (`TextBlock.cpp:161-163`) and image paths (`ImageBlock.h:34`), not a decompressor. `HEAP_ANALYSIS.md` measured page turns as heap-neutral. The 32 KB is reserved at `Epub::load` because the HTML was deleted (`Section.cpp:428`) and the next index, the next settings change, or KOReader will ask the ZIP again — and because a 32 KB allocation after fragmentation does not exist. Changing the on-disk representation is how you make "never needed" true for the session, not just for the turn.

If idea 3's cache-hit experiment shows largest-block already recovering without a convert, ship that first and treat idea 1 as the follow-up that makes rebuilds cheap. If it does not — if `ensureSharedWindow` on cache hit is the whole 9 KB plateau — idea 1 is the structural move and the section cache was necessary but not sufficient.

---

## Files / functions to touch (idea 1)

- `lib/ZipFile/ZipFile.cpp` / `.h` — STORED writer; optional `sumUncompressed()`; stop using `InflateReader::init(true)` for convert (use `InflateStream`)
- `lib/InflateReader/InflateReader.cpp` — ideally never `ensureSharedWindow` for converted books; `g_sharedWindow` becomes dead for the reader path
- `lib/miniz/src/InflateStream.cpp` — already the right engine
- `lib/Epub/Epub.cpp` — convert-before-reserve sequencing; do not defensive-reserve when convert is pending
- `src/activities/ActivityManager.cpp` / `EpubReaderActivity.cpp` — convert under loan in the same place we already popup+loan for indexing; optional `heapDirtyFromIndexing_` after convert
- `lib/Epub/Epub/BookMetadataCache.cpp` — `hasAnyDeflated` after convert is naturally false; no format bump required if we replace the zip
- `src/network/server/CrossPointWebServer.cpp`, `OpdsBookBrowserActivity.cpp` — unchanged except convert runs on next open after `clearRenderCache`
- Tests: `test/host/test_conditional_inflate_window.cpp`, new STORED-writer fixture from `zip_deflate_fixtures.h`

Do not touch `PngToFramebufferConverter`, fonts, or the section.bin format. Those are other dictionaries and another cache.

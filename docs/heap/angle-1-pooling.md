# Allocation-site census and pooling

Angle: which heap *blocks* dominate the 370/640 counts, and what a pool/arena/slab would actually do. Not a threshold retune. Not the inflate window. Not the framebuffer loan.

Device figures used below are the post-fix table in `docs/HEAP_ANALYSIS.md` (Home 11,500 / 8,180 / 639; reading 36,460 / 15,860 / 381) plus the pre-fix walk that still has the only block-count time series (boot Home 597, book-open 380, page-turn plateau 382, Home+server 642).

There is no per-callsite heap dump on device (`CMD:HEAPPROF` is totals). The census is site × cardinality from the code, then matched against those totals. Where a number is inferred it is labelled.

---

## The shape of the 381 / 639, before any pool

Two different populations. Mixing them is how a pool gets aimed at the wrong number.

**The Page is not in the 381.** `EpubReaderActivity.cpp:1991-2032` loads a `unique_ptr<Page>`, hands it to `renderContents` by value (`:2158`), and the object dies when that function returns. Page turns measure heap-neutral (36,360 free, 382 blocks, flat across five turns) *because* the previous page is gone. Pooling PageLine/TextBlock will not subtract 200 from the settled reading count. Anyone who sizes a pool against “381 live blocks, therefore 381 page objects” is solving a number that is not on the heap at rest.

**The Page is still the site that dominates block *creation*, and it is the fragmentation event.** The same analysis document records largest-block collapsing 36,852 → 17,396 on open → 9,204 after the first render, then never moving. That second step is deserialize + font prewarm overlapping, then a mass free. TLSF cannot compact; it can only coalesce. Two hundred small allocs interleaved with persistent glyph buffers, then freed, is exactly how a 36 KB free heap becomes a 16 KB largest run.

**Settled 381 (reading) is the firmware baseline plus a handful of large session objects.** After the page is gone, what remains of our code is:

| Site | Blocks (inferred) | Evidence |
|---|---:|---|
| ESP-IDF / Arduino / USB-CDC / NVS / SPIFFS / WiFi-STA | ~150–250 | WiFi is *kept* when the reader stops the server (`BG_WIFI.stop(/*keepWifi=*/true)` at `EpubReaderActivity.cpp:300-301,351-352`). Present in both Home and reading. |
| `GfxRenderer::fontMap` | 28 | `BuiltinFonts.cpp` has 28 `insertFont` calls; `std::map<int, EpdFontFamily>` at `GfxRenderer.h:56` is one rb-node each. Families themselves are flash pointers (`EpdFontFamily.h:32-35`). |
| Font prewarm slots | 8–12 | `FontDecompressor.cpp:368-369`: one `malloc` for bitmaps + one for the lookup, × up to `MAX_PAGE_SLOTS=4` (`FontDecompressor.h:14,54`). Plus hot-group / hotGlyphBuf. This is the *right* pooling pattern already: flat buffers, not per-glyph. |
| Inflate window | 1 | 32,768-byte `g_sharedWindow` (`InflateReader.cpp:19,37,65-74`). Permanent for the book. Do not free. |
| `Epub` + `Section` + `CssParser` (empty) + `BookMetadataCache` | ~15–30 | Spine/TOC live on SD with a one-entry memo (`BookMetadataCache.h:78-83`). CSS **released** after warm open (`Epub.cpp:446-450,481-489`). |
| Selection index (if compiled in) | 1–20 | `selectionPageIndex` (`EpubReaderActivity.h:91`) is one `vector<SelWord>`. `SelWord::text` is `std::string` (`SelectionModel.h:10-17`); 24-byte SSO on this target (`WordVectorGrowth.h:22-28`) holds typical English. Overflow strings only for long tokens. Hundreds of words ≠ hundreds of blocks. |
| Everything else (settings POD, i18n in flash, activity) | tens | `I18n.cpp` returns `const char*` into generated tables. |

That adds up to ~380 without a live page. It matches the measured 381. **The 381 is not a pooling target.** It is mostly someone else’s allocator plus ~40 session blocks we already flattened (fonts, inflate, CSS release, metadata-on-SD).

**Settled 639 (Home) is the 381-class baseline plus Home UI plus the HTTP server, minus the reader’s large objects.**

- Boot Home, server not yet up: **597** blocks, 51 KB free.
- Home + server: **642** blocks, 11.5 KB free. Delta **~45 blocks / ~22 KB**.
- `CrossPointWebServer::mountRoutes` (`CrossPointWebServer.cpp:326-411`) is **51** `server->on(...)` calls, each an Arduino `FunctionRequestHandler` + `std::function` + `String` URI. 45 extra blocks for “server came up” is the right order of magnitude. The 22 KB is the task stack, buffers, mDNS, not the 45 headers.
- Cover cache is **one** 52 KB `malloc` (`HomeActivity.cpp:995`) and is often *skipped* by `HomeCoverCachePolicy` at 11 KB free. It cannot explain 639 blocks.
- `RecentBook` is four strings (`RecentBooksStore.h:9-16`). Ten recents is tens of blocks, not hundreds.

Pooling PageLine does nothing to Home. Pooling HTTP handlers might recover ~45 blocks and ~1 KB of headers — real, small, not the binding constraint (Home’s problem is the 22 KB server *payload* and the 8 KB largest run, not 45 headers).

---

## Per-operation census: the page explode

This is the site that actually dominates *count*.

A `TextBlock` is one rendered line (`TextBlock.cpp:208-209`). It owns five parallel vectors (`TextBlock.h:15-28`):

```
words            std::vector<std::string>     // 24 B/slot + overflow heap if >15 chars
wordXpos         std::vector<int16_t>
wordStyles       std::vector<EpdFontFamily::Style>
wordFocusBoundary std::vector<uint8_t>        // empty ⇒ no heap when focus is off
wordFocusSuffixX  std::vector<uint16_t>       // empty in lockstep
```

`Page::elements` is `vector<shared_ptr<PageElement>>` (`Page.h:126`). `PageLine` holds `shared_ptr<TextBlock>` (`Page.h:33-34`). Deserialize never uses `make_shared`. It does this:

1. `new (nothrow) Page` — `Page.cpp:413`
2. `elements.reserve(count)` — one vector buffer — `:431`
3. Per line, `TextBlock::deserialize` (`TextBlock.cpp:193-291`):
   - `words.resize` / `wordXpos.resize` / `wordStyles.resize` — **3 buffers** — `:225-227`
   - per word, `readBoundedWord` → `word.resize(len)` — `:33` — heap only if `len > 15` (SSO)
   - optional focus `resize` × 2 — `:254-255`
   - `new (nothrow) TextBlock` — `:284`
   - `unique_ptr<TextBlock>` returned
4. `new (nothrow) PageLine(std::move(tb), …)` — `Page.cpp:68`. The constructor takes `shared_ptr<TextBlock>`, so the unique_ptr conversion **allocates a control block**.
5. `page->elements.push_back(std::move(pl))` — `:445`. `unique_ptr<PageLine>` → `shared_ptr<PageElement>` **allocates another control block**.

**Blocks per text line, no focus, all words in SSO (typical English):**

| # | What | Site |
|---|---|---|
| 1 | `TextBlock` object (~92 B: vptr + 5×12 B vector + `BlockStyle`) | `TextBlock.cpp:284` |
| 2 | `shared_ptr` control block for TextBlock | `Page.cpp:68` ctor |
| 3 | `words` backing | `TextBlock.cpp:225` |
| 4 | `wordXpos` backing | `:226` |
| 5 | `wordStyles` backing | `:227` |
| 6 | `PageLine` object | `Page.cpp:68` |
| 7 | `shared_ptr` control block for PageLine | `Page.cpp:445` |

**7 TLSF blocks per line**, plus the page object and the `elements` array. Focus reading adds 2. CJK (3 bytes/codepoint, SSO fails at 6 characters) adds **one block per overflowing word**.

Layout (cold index) is almost as bad. `ParsedText::extractLine` does `new TextBlock` then `shared_ptr<TextBlock>(tb)` (`ParsedText.cpp:1229-1235,1280-1286`) and `ChapterHtmlSlimParser::addLineToPage` does `std::make_shared<PageLine>` (`ChapterHtmlSlimParser.cpp:2152`) — `make_shared` folds object+control into one, so **6 blocks/line** during index rather than 7. The paragraph’s five `ParsedText` vectors (`ParsedText.h:22-26`) and `computeLineBreaks`’s `dp`/`ans` (`ParsedText.cpp:616-617`) sit on top until the block is consumed.

`Serialization.h:11-14` already says a page deserialize is “~1000 field reads of 1–4 bytes”. That is the same fact in I/O form: a page is hundreds of tiny records.

### Cardinality on this panel

800×480, default `screenMargin = 5` (`CrossPointSettings.h:376`), 16 pt reader font, `advanceY` typically 22–26 px (`GfxRenderer.cpp:2058-2065`). Status bar eats a line. Usable height:

- Portrait ~760 px → **~30–34 lines**
- Landscape ~420 px → **~16–20 lines**

Words per line ~10–14 (English). Words per page ~250–400. Serialized payload (length-prefixed strings, xpos, style, `BlockStyle`) is **~4–8 KB**. Heap after explode is **~15–25 KB of payload + 7×N × 16 B headers**.

### Worked page (portrait, 32 lines, 12 words, no focus, SSO)

| | Blocks | Headers @ 16 B | Payload (order-of) |
|---|---:|---:|---:|
| Page + elements vector | 2 | 32 | ~0.5 KB |
| 32 × (TextBlock + 2 control + 3 vectors + PageLine) | **224** | **3,584** | ~18 KB (`words` 12×24, xpos, styles, objects) |
| Word overflow | 0–20 | — | rare in English |
| **Total live during renderContents** | **~230** | **~3.7 KB** | **~19 KB** |

230 blocks is **60% of the entire reading count**, live for the whole of `renderContents` — including font prewarm (`EpubReaderActivity.cpp:2169-2171`: scan-pass `page->render` *then* `endScanAndPrewarm`, which mallocs at `FontDecompressor.cpp:368-369`) and the 8,000-byte AA strip (`:2269-2277`). Then the 230 are freed into a heap that now has glyph slots sitting in the middle of where they were.

That is the offender. File:line of the top sites, ranked by blocks created *per page*:

1. **`TextBlock.cpp:225-227`** — 3 vector buffers × lines. Biggest single family.
2. **`Page.cpp:68` + `:445`** — 2 `shared_ptr` control blocks × lines. Pure tax: 32 B/line of allocator header for two refcounts that never share (a `PageLine` is the unique owner of its `TextBlock`; nothing else holds it).
3. **`TextBlock.cpp:284` + `Page.cpp:68`** — 2 objects × lines.
4. **`TextBlock.cpp:33` (`readBoundedWord`)** — 0–N per line; dominates on CJK / long German.
5. **`ParsedText.cpp:1229-1235` + `ChapterHtmlSlimParser.cpp:2152`** — same explosion on the *index* path, plus `ParsedText`’s paragraph vectors.

Cold open adds a second family that is *not* the page:

6. **`CssParser.h:157` `unordered_map<string, CssStyle>`** — one node + one key per rule, cap `MAX_RULES=1500` (`CssParser.cpp:48`). Reloaded for every `createSectionFile`, then `clear()`’d (`Section.cpp:546-548`). A 200-rule sheet is **200–400 blocks** during index. Warm reading has already `releaseMemory()`’d this (`Epub.cpp:450,489`). It is the cold-open 503+ story, not the 381.

---

## Ranked ideas

### 1. A 12 KB bump arena for the deserialized page (the move)

**Mechanism.** One TLSF block, held for the reader’s lifetime, bump-pointer inside it, `deallocate` is a no-op, `reset()` on page death. Every `new PageLine` / `new TextBlock` / vector backing / overflowing `string` during `Page::deserialize` comes from the bump. `renderContents` still sees the same types. When the `unique_ptr<Page>` dies, the arena resets; the 12 KB TLSF block is **not** returned to the heap.

Concretely:

```
class PageArena {
  uint8_t* mem;      // one malloc, 12288 bytes, 8-aligned
  size_t cap, used;
  void* alloc(size_t n);   // bump, 8-align; nullptr on overflow
  void reset();            // used = 0; no free()
};
```

- **Type:** `uint8_t[12288]` bump. Placement-new for `Page` / `PageLine` / `TextBlock` / `PageImage` / `PageHorizontalRule` / `PageTableFragment`. Custom allocator (`allocate` bumps, `deallocate` empty) installed on the five `TextBlock` vectors and on `Page::elements`. `std::string` overflowing SSO must also allocate from the arena (a thin `ArenaString` or a `std::basic_string` with that allocator). `shared_ptr` control blocks must come from the arena too — or, better, disappear (idea 3) so they are not in the bump at all.
- **Lifetime:** allocate in `EpubReaderActivity::onEnter` *after* `BG_WIFI.stop` (`:300-352`), hold as an activity member, `reset()` at the end of `renderContents` (or in `Page`’s destructor via a back-pointer), `free()` in `onExit`. Never free/re-acquire mid-session. That is the inflate-window trap (`HEAP_ANALYSIS.md`: free 32,768 → 32,756 usable → cannot get it back). A bump that is not returned to TLSF cannot have that bug.
- **Slab size: 12,288 bytes**, not 16,384 and not 32,768.
  - Typical packed page is 4–8 KB of payload. Exploded, the *data* is ~19 KB but that 19 KB is 32× three vector headers, SSO inline in `string` slots, object padding. In a bump with no per-block headers and no `shared_ptr`, the same page is the serialized size plus one `BlockStyle` per line: **~6–10 KB**.
  - 12 KB leaves ~2–4 KB of slack for an image path, a table fragment, footnotes (`FootnoteEntry` is 128 B inline, `Page.h:128`).
  - 12 KB fits in the post-server-teardown heap (measured recovery to ~33 KB free) and is 3 KB under the reading largest-block (15,860), so a one-time alloc at `onEnter` is realistic. 16 KB would be fighting that 15,860 on a subsequent process if anyone ever tried to recapture it; 32 KB is the window.
  - Overflow path: if `alloc` fails, fall back to the current TLSF deserialize for that page and log. Dense table pages (`MAX_PAGE_ELEMENTS=1024`, `MAX_WORDS_PER_TEXT_BLOCK=512`) can exceed 12 KB; they are rare and already the OOM path.
- **Freed when:** reader `onExit` only. Between pages, `reset()`. Home gets the 12 KB back when the book closes, which is when Home needs it.

**What it moves.**

- *During* `renderContents`: **~230 live page blocks → 1.** Headers 3.7 KB → 16 B. Peak allocated-block count during a turn drops by ~220. Settled 381 does **not** drop (page was never in it).
- *Largest free block, reading:* this is the number that should move. Today font prewarm mallocs *into* the swiss cheese the 230 allocs just cut (`:2169-2171` then `:368-369`). After, prewarm sees one remaining contiguous run. Honest range: **15.9 KB → 22–28 KB**, not the 33 KB in `HEAP_ANALYSIS.md` rec 7 (that bundled persistent AA scratch and assumed the arena was not itself resident). If the 12 KB stays allocated, total free falls 36.5 → ~24.5 KB; largest can still *rise* because 75% of today’s free is unreachable. The currency is largest, not free.
- *Home 639 / 8 KB largest:* unchanged while reading; +12 KB free / possibly a bigger largest when you return to Home (the arena is gone, and the heap was not shredded by the last page). Secondary, not the pitch.

**Cost.**

- RAM: 12 KB resident while reading. Paid for by not exploding 19 KB of scattered payload; net data is similar or slightly down (no control blocks, no 16 B × 230). Peak *contiguous* demand goes up by 12 KB at `onEnter`, which is the moment we have it.
- Flash: a ~80-line `PageArena` plus allocator typedefs on `TextBlock`/`Page`. Well under a kilobyte. No `std::pmr` (pulls extra binary). No format version bump.
- Latency: one `malloc` per book instead of ~230 per turn. Deserialize should get *faster* (no TLSF for every vector). Render unchanged.
- Complexity: medium. Custom allocator on `std::vector`/`string` is the sharp edge (must not `deallocate` into TLSF; must not mix with default-allocator vectors). Tables/images need the same allocator or they punch holes *around* the arena, which would recreate a smaller version of the bug.

**What would have to be true.**

1. A typical cached page’s *packed* size is ≤ ~10 KB. (If real books serialize at 20 KB, 12 KB is wrong and we size from the LUT span — see experiment.)
2. `onEnter` after server-stop still has a ≥12 KB contiguous run. Pre-fix book-open largest was 17 KB; post-fix reading largest is 15.9 KB *after* the shred. The alloc must happen **before** the first deserialize.
3. Nothing else in `renderContents` allocates from TLSF in the 1–200 B class besides font prewarm and the AA scratch. (Annotation/selection copy words *out* of the page into `selectionPageIndex`; that copy must stay on TLSF or it becomes the new pepper. It is 1 vector, acceptable.)
4. Overflow fallback is actually hit only on tables/images, not on ordinary prose — otherwise we have two implementations and the prose path still shreds.

**Cheap experiment (half a day, no architecture).**

Do not build a pool. Add a `thread_local` counter incremented at every allocation site in `Page::deserialize` / `TextBlock::deserialize` / `readBoundedWord` (the `new`s and the `resize`s). Log one line per page:

```
[PGE] page blocks: lines=32 words=348 heap_ops=241 serialized_est=6120
```

`serialized_est` = `lut[next]-lut[this]` from `Section.cpp:587-616` (page span is already knowable; we just never use it). Run `device_walk.py` heap + one chapter of page turns.

- If `heap_ops` is ~7×lines and `serialized_est` is 4–10 KB, idea 1 is live. Size the slab to `max(serialized_est)+25%` from that trace, not from this document.
- If `heap_ops` is ~lines (i.e. vectors are empty? impossible) or `serialized_est` is 30 KB, kill the 12 KB number.
- If largest-block does **not** dip on the first render when you *skip* deserialize and render a dummy page, the shred is confirmed as this site; if it still dips, the offender is prewarm/AA and a page arena will not save you.

Host-side: the simulator’s `sim_heap.cpp` already tracks peak single alloc. Counting live blocks around `Page::deserialize` in `test_section_cache_deserialization.cpp` is a one-function change and does not need the device.

**Files:** `lib/Epub/Epub/Page.cpp` `:412-498`, `Page.h` `:123-144`, `lib/Epub/Epub/blocks/TextBlock.cpp` `:193-291`, `TextBlock.h` `:15-40`, `src/activities/reader/EpubReaderActivity.cpp` `:1991-2032,:2158`, `lib/Epub/Epub/Section.cpp` `:552-635`. New: `lib/Memory/PageArena.h` (or next to `HeapGuard.h`).

A stronger variant of the same idea, same experiment: **do not deserialize at all.** The section file already *is* the packed page (`Page::serialize` `:343-367`, `TextBlock::serialize` `:134-190`). Read the LUT span into the 12 KB slab (`Section.cpp:600` has `pagePos`; next LUT entry is the end). Render by walking tags in place; `drawText` already wants a NUL-terminated stack buffer (`TextBlock.cpp:76-87` has a 40-byte focus prefix). `collectSelectableWords` (`EpubReaderActivity.cpp:2499`) walks the same blob. Zero C++ objects, zero vtables, zero `shared_ptr`, no custom `std::allocator`. Cost: a `PageView` interpreter instead of `PageElement::render`. Format version unchanged. This is the version I would actually build if the census log shows `serialized_est < 12 KB`. The bump-with-types version is the compatibility ramp.

---

### 2. A packed CSS rule slab, section-build lifetime only

**Mechanism.** `rulesBySelector_` is an `unordered_map<string, CssStyle>` (`CssParser.h:157`) with `MAX_RULES=1500`. Each insert is a node plus a key. The map is reloaded from `css_rules.cache` on every `createSectionFile` and then `clear()`’d (`Section.cpp:546-548`); `clear()` does **not** free the bucket array — they already know this, which is why `releaseMemory()` exists (`CssParser.h:124-127`) and is used on the warm-open path (`Epub.cpp:450,489`) but **not** after section build (`Section.cpp:547` calls `clear()`).

Replace the map, for the duration of one section build, with one blob: concatenated `{u8 keyLen, key[keyLen], CssStyle}` plus a 256-slot open-addressed index of offsets (256 × 2 B = 512 B). Lookup in `resolveStyle` (`CssParser.cpp:698`) becomes a hash into the index and a `memcmp` on the key. `DescendantRule` cap is already 100 (`CssParser.h:61`); put those in the same blob.

- **Type:** `unique_ptr<uint8_t[]> cssSlab` on `CssParser`, filled by `loadFromCache`.
- **Lifetime:** allocate at the start of `createSectionFile`, free at the end (or `releaseMemory()`). Not resident while reading.
- **Size:** 200 rules × (avg 16 B key + ~120 B `CssStyle`) ≈ 27 KB worst-ish; a real EPUB stylesheet after the “no supported property” filter (`CssParser.cpp:447-449`) is often tens to low hundreds of rules. Size from `ruleCount` in the cache header (`CssParser.cpp:899-914`) — they already almost do this with `reserve(ruleCount)` for the bucket array (`:921-923`). One alloc of `ruleCount * stride` instead of `ruleCount` nodes.
- **Freed when:** end of `createSectionFile`. Reading never holds it. Avoids the inflate trap because the next chapter build can size *down* (never need exactly 32,768) and because a miss can re-parse.

**What it moves.** Cold-open allocated-block count (503+) and cold-open min-free (4,164). A 200-rule map is ~200–400 blocks and a rehash is a second contiguous bucket array beside the old one (`CssParser.cpp:432-439` documents this). One slab: **400 blocks → 1**, and the rehash spike vanishes. Does not move reading 381 / 15.9 KB. Does not move Home.

**Cost.** Flash: rewrite `resolveStyle` and cache load/save. RAM: same order of bytes, one block. Latency: section build should get faster (no node chase). Complexity: medium; descendant selectors and grouped selectors must still work.

**What would have to be true.** Cold-open HEAPPROF during `createSectionFile` shows a block-count spike of hundreds that falls when the section file is written. If the spike is the *page* being built (idea 1’s layout path) and CSS is only tens of rules, this is a miss. `LOG_DBG("EBP", "Loaded %zu CSS style rules"` at `Epub.cpp:442` already prints the count — read it off the cold-open trace before writing a slab.

**Cheap experiment.** On a cold open, log `ruleCount`, `rulesBySelector_.bucket_count()`, and `heapguard::allocatedBlockCount()` immediately before and after `loadFromCache` / `parseCssFiles`. If Δblocks ≈ 2×ruleCount, build the slab. If Δblocks is ~10, kill it.

**Files:** `lib/Epub/Epub/css/CssParser.cpp` `:425-540,:899-928`, `CssParser.h` `:111-158`, `lib/Epub/Epub.cpp` `:446-450`, `lib/Epub/Epub/Section.cpp` `:546-548`.

---

### 3. Delete the two `shared_ptr`s per line (one-day slice of #1, ships without an arena)

**Mechanism.** A `PageLine` uniquely owns its `TextBlock`. A `Page` uniquely owns its `PageElement`s. There is no sharing. `shared_ptr` here is a historical accident that costs **two TLSF blocks and ~32 B of headers per line** for refcounts that never go above 1.

Change:

- `Page::elements` to `vector<unique_ptr<PageElement>>` (`Page.h:126`)
- `PageLine::block` to `unique_ptr<TextBlock>` (`Page.h:33-34`)
- `ParsedText`’s `processLine` callback from `function<void(shared_ptr<TextBlock>)>` (`ParsedText.h:94`) to `void(*)(void*, unique_ptr<TextBlock>)` or a small `struct { void* ctx; void (*fn)(void*, unique_ptr<TextBlock>); }` — `std::function` is already on the project’s banned-in-hot-path list (CLAUDE.md). Layout currently heap-allocates a control block at `ParsedText.cpp:1235` *and* a `std::function` at the call site.
- `ChapterHtmlSlimParser.cpp:2152` `make_shared<PageLine>` → `makeUniqueNoThrow<PageLine>`
- Table cells (`Page.h:79`) similarly unique.

**What it moves.** 2 of the 7 blocks per line, immediately, with no arena. 32-line page: **64 fewer blocks, ~1 KB fewer headers**, during the shred window. Largest-block recovery is real but smaller than #1 (the three vector buffers remain the pepper). Settled 381 unchanged.

**Cost.** Flash: mechanical type change across `Page`, `ParsedText`, parser, markdown (`MarkdownRenderer.cpp` uses the same `Page`), host tests. RAM: slightly down. Complexity: low-medium (callback signature is the annoying part). No format bump.

**What would have to be true.** No remaining `shared_ptr<TextBlock>` alias (grep the parser’s table path at `ChapterHtmlSlimParser.cpp:537`, which currently copies a `shared_ptr` into a cell — that becomes a `unique_ptr` move). If something actually shares a `TextBlock` across two `PageLine`s, this is wrong. I found no such share: each `extractLine` makes a new `TextBlock`.

**Cheap experiment.** The same `heap_ops` log as idea 1, after this change only: expect `heap_ops` to drop by `2×lines`. If it does not, the control blocks were not a separate malloc (some `make_shared` path we missed) and this idea is empty.

**Files:** `Page.h:33-34,79,126`, `Page.cpp:24,68,445`, `ParsedText.h:54,94`, `ParsedText.cpp:495,908,1235,1286`, `ChapterHtmlSlimParser.cpp:537,2152`, `lib/Markdown/MarkdownRenderer.cpp`, `test/host/test_section_cache_deserialization.cpp:112`.

Do this first if #1 is accepted. An arena full of `shared_ptr` control blocks is a wasted arena.

---

## Why the obvious answers are wrong here

**“Pool the 381 reading blocks.”** Most of them are not ours, and the ones that are (font slots, inflate window, empty CSS parser, one-entry metadata memo) are already one-or-few large blocks. A slab for `std::map` font nodes saves 28 × 16 B = 448 B and does not change largest-free. The 381 is a solved shape.

**“`make_shared` everywhere.”** Folds object+control (7 → 5 per line). Still five TLSF boundaries per line. Still interleaves with prewarm. Flash-cheap, structurally nothing.

**“Intern words / drop `std::string`.”** SSO already holds 15 bytes (`WordVectorGrowth.h:22-28`). English prose does not heap-allocate per word. Interning “the”/“of” across a page that is about to be freed *creates* a permanent dictionary, which is how you make the 381 worse. CJK overflow is real; an arena (idea 1) eats it without a dictionary.

**“Pool font glyphs.”** Already done: 4 flat slots, two mallocs each (`FontDecompressor.h:54-68`, `:368-369`). Per-glyph `malloc` is not the 381 and not the shred. The glyph buffers are the *survivors* of the shred — they are why coalescing fails.

**“Pool the Home cover buffer / recent-book strings.”** Cover is one malloc and often refused. Recents are tens of strings. Home’s extra vs reading is ~45 HTTP handlers (idea-sized, not problem-sized) plus the 22 KB server payload, which is not a block-count problem.

**“A 32 KB page arena, symmetric with the inflate window.”** The window taught this lesson. Freeing a 32,768-byte block yields 32,756 usable; the next 32,768 fails. A page arena that is ever freed and recaptured at the same size will reproduce it. 12 KB, held until `onExit`, never recaptured.

**“Put the arena in `.bss`.”** 12 KB static would shrink the heap for Home, which already sits at 11.5 KB free. Home is the worst state on the device. The arena is a reader object.

**“Pool ParsedText’s five word vectors for layout.”** Those live for one HTML block during index, then `erase` (`ParsedText.cpp:560-568`). A reusable `ParsedText` member (allocate in `ChapterHtmlSlimParser`, `clear()` between blocks) is a good hygiene patch and saves the geometric-growth churn `WordVectorGrowth.h` exists to police. It does not change reading largest-block. Do it as a side effect of #1’s allocator, not as the design.

**“AA scratch pool (8,000 B permanent).”** That is rec 7’s other half. It is one block already (`makeUniqueNoThrow<uint8_t[]>` per page at `:2277`, freed at end of `renderContents`). Making it permanent saves a 8 KB alloc/free per turn, which is the *good* kind of TLSF use (one large block). It does not change block *count* in any interesting way. Do not confuse it with this census. If you do it, allocate it at `onEnter` next to the page arena so the two large reader blocks are adjacent and early.

---

## What I would actually do

There is a structural move, and it is not “the architecture is already right.” The allocation *model* for a page is wrong for a TLSF heap with no compaction: one SD blob is exploded into ~7 blocks per line, interleaved with the only persistent allocations the reader still makes (font slots), then discarded. Settled block counts hide it because the evidence is gone by the time `CMD:HEAPPROF` runs.

1. Run the one-line census log. If `heap_ops ≈ 7×lines` and `serialized_est < 12 KB`, proceed.
2. Ship idea 3 (`unique_ptr`) the same week — it is the prerequisite and a measurable 2×lines drop by itself.
3. Build idea 1 as **blob-render into a 12 KB `onEnter` arena**, not as `std::vector` with a custom allocator, unless blob-render trips on tables/images. Tables already have a separate tag; they can keep the old explode path.
4. Only then look at idea 2, and only if the cold-open Δblocks around CSS load is hundreds.

If the census log shows `heap_ops` of ~20 for a 32-line page, I was wrong: something is already pooling and this document should be thrown away. I do not think that is the code I read.

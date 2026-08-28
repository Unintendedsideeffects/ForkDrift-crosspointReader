# R4 — Adversary: is there a structural win, or is this the limit?

Read-only. No builds. Citations are `file:line` in `/home/malcolm/Code/ForkDrift/crosspoint-reader`.
Numbers labelled *measured* are from `docs/HEAP_ANALYSIS.md` (post-fix table unless noted) and `docs/heap/lens-4-budget.md`.

---

## Confidence ranking (read this first)

| Rank | Claim | Confidence |
|---|---|---|
| 1 | Home 11.5 KB / 8.2 KB is an **occupancy** problem (Always-on HTTP + Wi‑Fi), not a fragmentation problem. 8,180/11,500 = 71% of free already sits in one run. | **High** — arithmetic on the post-fix table |
| 2 | Home is worse than reading *because the reader tears the server down and Home then puts it back*. The ~25 KB free-heap gap is the measured 22,324 B server plus ~4 KB of TLSF headers on the extra ~260 blocks. | **High** — teardown trace + `blocksBackgroundServer()` |
| 3 | The ~600 live blocks at boot Home are **not** pages, not activity churn, and mostly not yours. Boot Home is already 597 blocks *before* any navigation. | **High** — pre-fix walk block counts |
| 4 | `ActivityManager` create/destroy is **not** the fragmentation engine. It is a ~2–3 KB overlap tax on open. | **High** — measured Home↔Reader deltas |
| 5 | The page-object explode is a **reading-path TIME collision** (deserialize overlapping font-slot mallocs), then a plateau. It is absent from Home rest-state. | **High** for “not in the 381”; **medium** for arena recovery numbers |
| 6 | If Always-mode serving stays the idle default, **there is no large firmware-internal win left at Home**. Remaining pool growth is ~7–11 KB static (FTM, upload buffer, RTC logs, ringbuf IRAM). | **High** for “no 20 KB trick that keeps the server”; **medium** for the 7–11 KB bundle (linker map, not re-measured this pass) |
| 7 | Reading is **not** at the hardware limit. Two unverified bets remain (STORED-shadow C4; page bump/packed view). Both were designed, neither was Gate-0 measured. | **Medium** — design exists; falsifiers were written and not run |
| 8 | The 12,288 A/B is a real TIME hole. Pinning it on `OtaWebCheck` at a 52 KB Home heartbeat is **weak**: that task is only started from an HTTP handler that does not exist until the server has already eaten ~22 KB. | **Low / speculation** for identity; **high** that *some* 12,288-class transient split the run |

The owner's "we haven't found a good solution" is true **if and only if** "turn off Always-on HTTP at idle" is defined as not a solution. That is a product veto, not an engineering dead end. Treating it as an engineering dead end is how this hunt stayed on inflate windows, stacks, and thresholds.

---

## 1. Is the architecture near its limit, or is a large win still hiding?

**Split the device. The hunt mixed two different machines.**

### Home + Always-server (11,500 free / 8,180 largest) — near the *composition* limit, not the silicon limit

Post-fix (`docs/HEAP_ANALYSIS.md:87`):

| State | free | largest | frag% = 1 − largest/free |
|---|---:|---:|---:|
| Home + background server | 11,500 | 8,180 | **29%** |
| Reading, steady | 36,460 | 15,860 | **57%** |

The slogan “fragmentation, not total free, is the binding constraint” is true **while reading**. It is false at the worst measured state. At Home, 8,180 of 11,500 free bytes are already contiguous. Perfect coalescing of the remaining 3,320 B of riblets would not produce a 16 KB run. The heap is tight because **168 KB of a ~179 KB pool is live**.

Where those bytes went is measured, not inferred:

- Server objects stop+delete recover **22,324 B** (`docs/HEAP_ANALYSIS.md:155–156`; breakdown in `docs/heap/lens-4-budget.md:149–168`: routes −12,028, WS+UDP −3,992, mDNS+settle ~−5,700, plus the 8,192 B `bgwifi` stack at `src/network/background/BackgroundWifiService.h:52`).
- `getHeapSize()` itself shrinks ~4 KB while the server is up (641 vs 382 allocated blocks × ~16 B TLSF/poison; `docs/heap/linker-analysis.md:94–108`).
- Cover RAM cache and carousel RAM frames **did not allocate** in these traces. They are gated off (`src/activities/home/HomeCoverCachePolicy.h:43–61`; carousel skip at heap 45,328 in lens-4). They are not hiding in the 11.5 KB.

The silicon still has a 191,280 B raw heap region (`docs/heap/linker-analysis.md:63–70`). Home is not “the C3 is full”. Home is “we parked a full `CrossPointWebServer` — 51 `server->on(...)` sites at `src/network/server/CrossPointWebServer.cpp:326+` — on the idle screen, with `ENABLE_BACKGROUND_SERVER_ALWAYS=1` in `platformio.ini:120` and Always selected whenever `wifiAutoConnect` is set (`src/CrossPointSettings.h:477–480`).”

**If Always-on serving is non-negotiable, say so and stop hunting 32 KB dictionaries for a state that does not use them.** The leftover engineering at Home is the linker bundle: unused Wi‑Fi FTM BSS (~2.8 KB), `BufferedHttpUpload` buffer 4096→2048, RTC log depth, non-ISR ringbuf out of IRAM (`docs/heap/linker-analysis.md:295–312`). That is **7–11 KB into the pool**, not a new architecture. Flash is 91.9% full (`docs/heap/linker-analysis.md:293–294`); a custom allocator or a STORED converter spent on *Home* is the wrong patient.

### Reading (36.5 KB free / 15.9 KB largest) — not at the limit; the remaining wins are unverified, not absent

Reading's plateau is a different shape: more free, **worse** ratio, because a 32,768 B process-lifetime window sits wherever `ensureSharedWindow()` won it (`lib/InflateReader/InflateReader.cpp:10–20,74+`; claimed from `lib/Epub/Epub.cpp` on load) and because the first `renderContents` explodes a page into ~230 TLSF blocks and then frees them around newly-pinned glyph slots (see §2).

Two structural bets were written down and **not falsified**:

1. **C4 STORED shadow** (`docs/heap/architecture-verdict.md`). Removes the 32 KB pin from steady reads. Gate 0 (deflated vs `zip -0` twins, warm-cache, clean reboot, median largest-block) is the cheap experiment. It has not been run. The verdict's own falsifier is explicit: if STORED improves largest-block by **< 16 KB**, C4 is the wrong first ship (`architecture-verdict.md:344–348`).
2. **Page bump / packed `PageView`** (`docs/heap/angle-1-pooling.md:126–188`). Does not shrink the 381 rest count (the page is already dead after `renderContents`). It changes the *shape* of the first-render shred. Predicted largest-block 15.9 → 22–28 KB, **unmeasured**. The cheap census log (`heap_ops` × lines, LUT span) was also not run.

Freeing the inflate window on book close was tried and reverted because the hole is 12 bytes too small to hold *itself* again (`docs/FINDINGS.md` 2026-08-28T12:10Z; `CONTEXT.md`). That is a TIME/size-match trap, not proof that 32 KB cannot be recovered. C4's point is to never need that exact size again.

**Plain sentence the hunt has been unwilling to write:** on a 179 KB single-pool TLSF heap with no compaction, with Always-on HTTP as idle, **Home is about as good as this composition gets**. Reading still has one or two large engineering bets, both gated on measurements that were specified and skipped. Calling the whole device “solved except nits” because Home cannot yield 20 KB without deleting the server is a category error.

---

## 2. What has everyone been systematically blind to?

The hunt's frame — 32 KB inflate window, framebuffer, task stacks, HeapGuard floors — is the set of *named large objects and named constants*. That frame misses four facts that are already in the traces.

### 2.1 The ~600 blocks are a foreign allocator plus ~40 of yours

Pre-fix walk (`docs/HEAP_ANALYSIS.md:59–69`):

| State | alloc blocks | free heap |
|---|---:|---:|
| Boot, Home, server not yet | **597** | 51,464 |
| Reading plateau | **382** | 36,360 |
| Home + server | **642** | 11,228 |

Boot Home is already 597 live blocks **before any activity has been destroyed**. Navigation is not how you get to 600.

Angle-1 already decomposed this (`docs/heap/angle-1-pooling.md:11–41`) and was not used as a stop. Settled reading 381 is ESP‑IDF / Arduino / USB‑CDC / NVS / SPIFFS / Wi‑Fi STA (~150–250) + 28 `fontMap` rb-nodes + a handful of font slots + one inflate window + tens of session objects. Settled Home 639 is that population **minus** the reader's large objects **plus** ~45 HTTP handler/function/String nodes (51 `server->on` calls) plus Home strings. Cover cache is one 48 KB malloc when it runs; it cannot explain 639 counts, and it is skipped here.

TLSF tax: 641 × 16 B ≈ 10 KB of the 16 KB hidden from `ESP.getHeapSize()` (`docs/heap/linker-analysis.md:101–108`). Pooling 100 of *your* blocks saves ~1.6 KB of headers. Pooling PageLine saves **zero** at rest — see 2.3.

**Blindness:** counting 600 as if it were a firmware census of page objects / activity leaks.

### 2.2 Activity lifecycle is the overlap tax, not the engine

`ActivityManager::replaceActivity` defers destroy-to-avoid-`delete this` (`src/activities/ActivityManager.cpp:239–245`). `goToReader` now stops both background servers *before* the factory (`:323–336`), which was the high-value load-path fix. The factory still runs while Home is alive:

```
goToReader → BG_WIFI.stop + BackgroundWebServer::stop
          → ReaderRegistry::open → features::epub::createActivity
                → loadDocumentNoThrow<Epub>   // Epub::load, inflate reserve
                → createActivityNoThrow<EpubReaderActivity>
          → replaceActivity  // Home onExit later, on the next loop()
```

(`src/features/epub/Registration.cpp:23–36`; `ActivityManager.cpp:311–348`.)

Measured cost of that overlap, once the server is already down: leaving the reader reclaims **~3 KB** (`lens-4-budget.md:131–141`: exit EpubReader 44,272 → enter Home 47,440). During the *old* overlap with the server still up, exit Home was −2,360 B (`lens-4:179`). FINDINGS 2026-08-28T10:45Z claimed Home still held cover/carousel buffers through parse. That is stale against current gates: `HomeActivity::onExit` frees the cover and invalidates carousel (`src/activities/home/HomeActivity.cpp:959–967`); `storeCoverBuffer` refuses at this heap (`HomeCoverCachePolicy.h:47–61`); traces show disk-only carousel.

`goHome()` always `make_unique<HomeActivity>` (`ActivityManager.cpp:364–372`). That is one object + `recentBooks` strings (`HomeActivity.cpp:352–374`), not hundreds of blocks. Reading *drops* block count 597 → 380. If create/destroy were the engine, the count would ratchet. It does not.

**Blindness:** “heap-allocated activities on every navigation” sounds like a fragmentation story. The measurements say it is a 2–3 KB ordering bug, already half-fixed (servers), with Home-still-alive during `Epub::load` as the remainder.

### 2.3 Section-cache / page-object design: rest-state success, shape failure

`EpubReaderActivity` loads a `unique_ptr<Page>` and hands it into `renderContents` by value; the page dies when that function returns (`src/activities/reader/EpubReaderActivity.cpp:1991–2032`, `:2158` in angle-1). Page turns are heap-neutral (36,360 free, 382 blocks, five turns — `HEAP_ANALYSIS.md:73–74`) **because the cache-on-SD design worked**. The 381 is not 381 page objects.

What failed is the **explode shape**, live only during render:

- `Page::elements` is `vector<shared_ptr<PageElement>>`; `PageLine` holds `shared_ptr<TextBlock>` (`lib/Epub/Epub/Page.h:33–34,123–127`).
- Cached deserialize does not `make_shared`: `TextBlock` `new`, three vector `resize`s, then two separate control blocks (`lib/Epub/Epub/blocks/TextBlock.cpp:225–227,284`; `Page.cpp` deserialize path as cited in `docs/heap/angle-1-pooling.md:59–70`).
- Worked portrait page: **~7 TLSF blocks/line × ~32 lines ≈ 224 blocks**, ~3.7 KB headers, ~19 KB payload, **during** `renderContents` (`angle-1-pooling.md:99–108`).
- Font prewarm mallocs *into that swiss cheese* (`EpubReaderActivity.cpp:2166–2172`; `FontDecompressor.cpp` page slots). Then the 230 blocks free. TLSF coalesces around the new glyph buffers. Largest block steps 36,852 → 17,396 → 9,204 and **never moves again** (`HEAP_ANALYSIS.md:122–127`). Post-fix the second step lands at 15,860, same plateau shape.

That is TIME: a mass of small allocs overlapping a persistent mid-size family, once per session. It is not Home. An arena / packed `PageView` is a reading-path topology change. Shipping it to “fix Home 8.2 KB” is aiming at the wrong state.

**Blindness:** treating “~600 blocks” and “page objects” as the same population.

### 2.4 TIME, not SIZE — including the hunt's own 12,288 lead

Same sizes, different *when*:

| Event | When it is taken | What it does to the rest of the session |
|---|---|---|
| Inflate 32,768 | First deflated `Epub::load`, after Home/settings/fonts exist | Splits the working heap for process lifetime (`InflateReader.cpp:10–20`). Lazy claim is documented as the worse path (`:38–42`). |
| First page deserialize + font slots | First `renderContents` | The shred event; then plateau. |
| `bgwifi` 8,192 + 16 KB of routes/WS/mDNS | Home's first paint, when `HomeActivity::blocksBackgroundServer()` becomes false (`HomeActivity.cpp:784–797`) | Occupies the idle state. Started *into* a heap that may already hold the inflate window. |
| Document factory | While outgoing Home still lives | ~2–3 KB overlap. |
| Transient 12,288 stack | See below | Frees the bytes, not the hole. No compaction. |

The 12,288 A/B (Home heartbeat free 52,584 vs 52,508, largest 38,900 vs 26,612) is the cleanest TIME exhibit in the brief: **free identical, largest differs by exactly one 12,288**. TRMNL is ruled out by its own log. The remaining named constant is `kOtaWebCheckStackBytes` (`src/network/ota/OtaWebCheck.cpp:18`).

**That identity is speculation.** `OtaWebCheck::start()` is reached only from `POST /api/ota/check` (`src/features/ota_updates/Registration.cpp:22–27`). There is no boot/Home heartbeat caller. A Home heartbeat at ~52 KB free is the *pre-server* state (`HEAP_ANALYSIS.md:60`: 51,464). The HTTP stack that could invoke OtaWebCheck does not exist yet. After the server exists, free is ~11 KB and a 12,288 task would fail the same way TRMNL does.

So either (a) something else of size 12,288 ran (Wi‑Fi/lwIP internal, USB path, 8192+4096 composite, a ForkDrift client hitting a server that *was* up earlier in that boot), or (b) the equality is coincidental. The hunt named the only two constants and crossed one off. It did not prove the other.

**Blindness:** SIZE of famous buffers. The damage is **when** a large run is punched out of a working heap, and whether anything persistent is allocated into the fragments it leaves.

### 2.5 Measurement bias the hunt never discounted

Every cited walk is USB‑CDC attached (`lens-4-budget.md:7`). `Logging.h` raises HWCDC RX to 8,192 when USB is present at boot (linker-analysis §5.4). Battery-in-bed Home is not this heap. That does not close a 22 KB server gap; it means the 11.5 KB figure is a **lab idle**, not a pocket idle. The hunt compared Home vs reading fairly (both USB). It then treated 11.5 KB as the device's nature.

---

## 3. Which single change most improves Home 11,500 / 8,180 — and why is Home so much worse than reading?

### Why Home is worse

Not because Home is “more fragmented”. It is *less* fragmented (29% vs 57%). It is worse because it is the only settled state in which the Always-mode server is allowed to live.

- Reader: `EpubReaderActivity::blocksBackgroundServer()` is true (`src/activities/reader/EpubReaderActivity.h:230`). `goToReader` stops the servers before load (`ActivityManager.cpp:333–336`). `onEnter` stops them again before layout (`EpubReaderActivity.cpp` comments at the teardown, ~309–356 historically). Wi‑Fi STA may be kept (`keepWifi=true`).
- Home: `blocksBackgroundServer()` is true only while recents are loading, before first render, while a cover is still painting, or while carousel is warming (`HomeActivity.cpp:784–797`). After first paint it returns **false**. `main.cpp` reconcile then starts Always-mode serving. That is the 47 KB → 11 KB collapse (`lens-4-budget.md:153–157`: Home-enter 47,440 → Home+server 11,148 = **−36,292**, of which server objects are 22,324 and the rest is task/Wi‑Fi/mDNS/settle).

Reading pays the 32 KB inflate pin and wins back the 22 KB server. Net +25 KB free vs Home. Largest block follows total free more than ratio: 16 KB of a shredded 36 KB heap still beats 8 KB of a packed 11 KB heap.

Home after a book also still holds `g_sharedWindow` (`InflateReader.cpp:17–20`; measured +3 KB on reader exit, not +35 KB — `lens-4:141`). So post-read Home = inflate pin + Always server. Boot Home before either is 51 KB / 37 KB largest. The worst state is **idle-with-advertised-feature after a book**, not “Home UI”.

### The single change

**Stop running Always-mode `CrossPointWebServer` as the Home idle default.**

That is the only change whose measured effect is the same order as the problem (22 KB objects + 8 KB task + the ~4 KB `getHeapSize()` shrink + the route-shaped 45 extra blocks). Home without those objects was measured at **47,440 free / 13,812 largest** (post-reader, inflate still pinned) and **51,464 / 36,852** (boot, no inflate). Either is a different device.

This is HEAP_ANALYSIS recommendation 5 (`docs/HEAP_ANALYSIS.md:224`). It was filed as “user's call, not engineering”. The adversary position: **that filing is why the hunt found no structural win.** The structure *is* “idle = full web stack on 179 KB”. Firmware nits cannot unmake that.

If Always is a product constraint, the honest Home answer is: **you are at the limit of this composition.** Next-best engineering, still not a 20 KB Home win:

| Next | Why it is smaller | Confidence |
|---|---|---|
| Release inflate on reader exit and **do not reacquire in-session** (C4 or reboot-before-next-open) | Home after reading would drop a 32 KB pin. Server start may then allocate *into* the 32,756 B hole and shred it — TIME again. The revert optimized for the *next* `malloc(32768)`, which Home does not need. | Medium — hole-vs-server-start unmeasured |
| Linker FTM + upload buffer + RTC logs | ~7 KB more pool, all states. Not Home-specific. | Medium (map-based) |
| Destroy Home before `Epub::load` | Helps the *open* min-free, not settled Home idle. ~2–3 KB. | High for size, low for Home idle |
| Page arena / unique_ptr PageLine | Reading largest-block. Zero at Home rest. | Medium |
| Shrink `bgwifi` 8192→4096 | HEAP_ANALYSIS already rejected as not-low-risk (`:286–288`): route handlers run on that stack (`BackgroundWifiService.h:51–52`). | — do not propose |
| Pool HTTP handlers | ~45 blocks, ~1 KB headers (`angle-1-pooling.md:41`). Not the 22 KB payload. | High that it is small |

`HomeActivity::blocksBackgroundServer()` already knows Home cannot paint and serve at once. Extending that to “Home never serves; File Transfer activity serves” is the same product decision with a named screen. That is still the win.

---

## Speculation (do not plan from these)

- C4 recovers ~20–30 KB largest-block while reading after reboot. **Speculation until Gate 0.**
- Page arena moves reading largest 15.9 → 22–28 KB. **Speculation until the `heap_ops` / dummy-page experiment.**
- OtaWebCheck is the 12,288 A/B. **Speculation; call graph argues against it at 52 KB Home.**
- Battery Home (no USB RX 8,192) is ~8 KB roomier. **Speculation; not in the walks.** Directionally real, does not replace the server.

---

## What would prove this report wrong

1. A HEAPPROF dump (not totals — a per-size histogram) at settled Home showing a 20 KB+ family that is **not** the web server, **not** Wi‑Fi STA, **not** the inflate window. Then there is a hidden win.
2. Gate 0: STORED twin improves reading largest-block by < 16 KB. Then C4 is not the reading win either, and reading *is* near the topology limit of page-explode + glyph slots.
3. Device walk with `backgroundServerMode=Never` (or File-Transfer-only) in which Home stays at ~11 KB / ~8 KB. Then I have mis-attributed the occupancy.

Until (1) or (3), the large win at the worst measured state is the one the hunt put in the “product” bucket and then tried to engineer around.

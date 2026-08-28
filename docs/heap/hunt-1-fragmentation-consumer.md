# R1 — identify the 12,288-byte consumer

Repo snapshot: `crosspoint-reader` on `fork-drift`. Read-only audit.
Arithmetic of the two Home heartbeats: `38,900 − 26,612 = 12,288` exactly.
Free heap almost unchanged (`52,584` vs `52,508`, Δ = 76 B). Min-free lower on the
failing run (`28,640` vs `33,776`). That is the signature of a transient block
carved out of the one large run, then freed: TLSF cannot compact, so the hole
stays even though the bytes come back.

No allocation in `src/` or `lib/` requests **exactly 12,288** except two
FreeRTOS task stacks. No `xTaskCreatePinnedToCore` anywhere. `lib/` has zero
`xTaskCreate`. `open-x4-sdk/` has zero `xTaskCreate`.

ESP-IDF `xTaskCreate` takes the stack size in **bytes** (not words). The stack
and TCB are both `pvPortMalloc` / heap-caps allocations. `vTaskDelete(nullptr)`
hands both to the idle task, which frees them. After idle has run, free heap
returns; contiguity does not, if anything smaller was allocated from the
remainder while the stack was live.

---

## Verdict (ranked)

| Rank | Candidate | Exact 12,288? | Intermittent? | At Home before book open? | Freed? | Fits “free back, largest halved”? |
|---:|---|---|---|---|---|---|
| 1 | **TerminusFetch task stack** | yes, literal | **yes** (`refreshDue` / credentials / sleep-mode / heap gate) | **yes, by design, in the 52 KB pre-server window** | yes, `vTaskDelete` | **best automatic fit** |
| 2 | **OtaWebCheckTask stack** | yes, literal | **yes** (only on POST) | only if a web server was up **and** someone POSTed **and** largest ≥ ~12.5 KB | yes, `vTaskDelete` | exact size, but **cannot actually succeed in either measured Home state** |
| 3 | JPEG MCU row (`16 × srcWidth`) | only if cover width is 768 | cache-hit vs miss | Home cover path is BMP-from-SD, not JPEG | yes (unique_ptr) | coincidental size, wrong decoder on Home |
| 4 | `bgwifi` 8,192 + `TimeSyncTask` 4,096 | sum is 12,288 | TimeSync is edge-triggered | TimeSync yes; bgwifi not resident at 52 KB | TimeSync yes; bgwifi only on stop | needs two adjacent holes to look like one 12,288; weaker |
| — | every other firmware task stack | no | — | — | — | wrong size or wrong phase |

**OtaWebCheck is not the consumer of the measured 12,288-byte Home hole.**
It is the right *size*, and it is heap-allocated and freed, but the gates make
a successful create impossible in the two Home heaps we actually have. The
consumer that *does* run in the 52 KB Home window, at exactly 12,288, under a
time gate, is TerminusFetch.

The “TRMNL is ruled out” log (`largest=9204`) is the **reading plateau**
(`docs/HEAP_ANALYSIS.md`: after first page turn, largest stays 9,204). It
proves a *later* fetch did not allocate. It does not prove a *Home, pre-server*
fetch never ran on the failing boot.

---

## 1. TerminusFetch — 12,288 B task stack (best fit)

**Size:** `TRMNL_FETCH_TASK_STACK = 12288` at
`src/features/terminus_sleep/Registration.cpp:351`.
Preflight asks for `12288 + 1024` (`:354`, `:392`) so `xTaskCreate` has TCB
room. Create at `:420`. Compile-gated by `ENABLE_TERMINUS_SLEEP` (on in
`platformio.ini:121` and the `full` custom profile).

**Heap-allocated?** Yes. Comment at `:348–350` states the 12 KB is
“heap-held only for the fetch's lifetime”. `xTaskCreate` → DRAM heap.

**Freed?** Yes. `terminusFetchTask` (`:382–387`) calls `vTaskDelete(nullptr)`
after `fetchAndPinTrmnlImage()`. Idle task frees stack + TCB. Extra HTTP
scratch is 2,048 + 2,048 (`TRMNL_HTTP_BUFFER_BYTES` at `:48, :193–194`), not
12,288, and dies with the client.

**What triggers it (Home, before a book is open):**

`BackgroundWifiService::run` connects STA, then **before** constructing
`CrossPointWebServer` calls `FeatureLifecycle::onBackgroundNetworkReady()`
(`src/network/background/BackgroundWifiService.cpp:176–185`) and **blocks**
until `backgroundStartupDeferred()` is false. That is the 52 KB Home window
(`HEAP_ANALYSIS.md`: “Boot, Home, server not yet up” ≈ 51–53 KB free /
≈ 36–39 KB largest).

`onBackgroundNetworkReady` (`Registration.cpp:466–476`) starts the fetch when
all of these hold:

1. `TERMINUS_STORE.hasCredentials()`
2. not already running, not in failure backoff
3. `forcedFetchRequested` **or** (`terminusFetchConfigured()` **and**
   `terminusFetchDue(true)`)
4. `terminusFetchConfigured()` = Terminus sleep mode active **and** credentials
   (`:435–437`)
5. `heapguard::canAllocate(13312)` — **passes** at Home largest ≈ 38,900;
   **fails** at reading largest 9,204 (the log that was taken as a full veto)

On success it logs
`Network ready; fetching before background server startup` (`:471`).
That line is the serial fingerprint of a Home-window 12,288 allocation.

Other start paths (not needed for the Home heartbeat, listed for completeness):

- `onBackgroundServerTick` (`:496–511`) does **not** create the task. If a
  refresh is due while serving, it **stops** the server (`keepWifi=true`) so
  the next pre-server hook can fetch. That can re-create the 52 KB window
  later at Home.
- `startTrmnlFetchAndWait` (`:717–721`) from sleep (`src/main.cpp:442,530`)
  and `TrmnlViewActivity.cpp:201` — sleep / extras, not idle Home.
- POST `/api/terminus/test` sets `forcedFetchRequested` and 202s; the actual
  stack is created after the server is recycled (`:643–657`).

**Intermittency:** `terminusFetchDue` / `refreshDue` against a 15-minute
default interval (`src/util/TerminusRefreshPolicy.h:12`), plus
`allowUnsetClock` monotonic pacing, plus `fetchTaskRetryActive` backoff, plus
sleep-mode and credentials. Two reboots of the same firmware therefore split:

- due at boot → 12,288 stack taken from the 38,900 run → remainder 26,612 →
  fetch ends, stack freed, a small pin (the 76 B) keeps the two sides from
  coalescing → Home heartbeat `MaxAlloc: 26,612` → inflate 32,768 fails
- not due at boot → no 12,288 → `MaxAlloc: 38,900` → inflate reserved

That is the whole bimodal pattern.

**Why the deferral log does not veto this:**
`Deferring fetch: no room for the 13312 B task stack (largest=9204)` is
`StartDecision::DeferNoHeap` (`:401–412`). `largest=9204` is the
post-page-turn plateau, not Home. A fetch that **already ran and freed** at
Home leaves exactly this later world: free heap restored, largest stuck near
26 KB until the book-open collapse takes it to 9,204, and a subsequent due
check then defers. Both observations can be true on one boot.

FINDINGS already recorded the 26,612 number on this path:
`[TRMNL] Repack skipped: no 32768-byte block (free=65364 largest=26612)`
(`docs/FINDINGS.md` 2026-08-15T16:05Z). Same hole, later victim.

**Min-free:** run B’s extra ~5 KB of min-free drop (`33,776 → 28,640`) is the
right order for stack + 2×2 KB HTTP, **not** for a 32 KB inflate (that
repack is skipped when largest is already 26,612).

---

## 2. OtaWebCheckTask — 12,288 B task stack (prime suspect by name; poor fit for the traces)

**Size:** `kOtaWebCheckStackBytes = 12288` at
`src/network/ota/OtaWebCheck.cpp:18`.
Create at `:83`. Compile-gated `#if ENABLE_OTA_UPDATES` (`:6, :63`); default
and `full` profile both set `ENABLE_OTA_UPDATES=1`
(`platformio.ini:130`, `include/FeatureFlags.h:309–310`).

**Heap-allocated?** Yes. Same ESP-IDF `xTaskCreate` path as Terminus. No
static stack. No `canAllocate` preflight (unlike TRMNL): it just tries
create and returns `StartTaskFailed` on `pdFAIL` (`:83–86`).

**Freed?** Yes. `otaWebCheckTask` (`:33–55`) stores the snapshot, `delete`s
the `OtaUpdater`, then `vTaskDelete(nullptr)` at `:54`. Stack + TCB return to
the heap. What does **not** return to zero is `OtaWebCheckData`
(`:22–31, :52`): `std::string latestVersion` / `message` stay in `Done`
until the next `start()`. Tens of bytes — the 76 B free-heap delta is the
right order for leftover strings, not an un-freed stack.

**What triggers it — complete gate chain:**

There is **one** caller of `OtaWebCheck::start()`:
`POST /api/ota/check` in `src/features/ota_updates/Registration.cpp:22–40`.

Gates, in order:

1. `ENABLE_OTA_UPDATES` — compile. If 0, `start()` returns `Disabled` (`OtaWebCheck.cpp:90–91`).
2. Route mounted only if `FeatureCatalog::isEnabled("ota_updates")`
   (`Registration.cpp:19, :93–97`), which is the same compile flag
   (`FeatureCatalog.cpp:96, :193–196`). No runtime toggle.
3. A `WebServer` must be running with `mountOtaRoutes` already applied.
   That happens at `CrossPointWebServer::begin()`, which in Always mode is
   **after** the Terminus pre-server window
   (`BackgroundWifiService.cpp:176–195`).
4. `WiFi.status() == WL_CONNECTED` else 503 (`Registration.cpp:23–26`).
5. Not already `Checking` (`OtaWebCheck.cpp:64–66`) → 200 `"checking"`, no
   second stack.
6. `new (std::nothrow) OtaUpdater()` (`:77–82`).
7. `xTaskCreate(..., 12288, ...)` (`:83`). Needs ~12,288 + TCB contiguous.

`GET /api/ota/check` (`Registration.cpp:43–69`) only reads the snapshot.
It never creates a task.

**Not automatic.** Firmware never calls `start()` on boot, Home enter, WiFi
up, or book open. User-facing triggers:

- Settings HTML button `checkForUpdates()` → `POST /api/ota/check`
  (`src/network/html/SettingsPage.html:316, :809–818`). Page load does not POST.
- ForkDrift Android `OtaScreen.kt` → `repository.startOtaCheck()` (user on
  the OTA screen, not idle Home). USB/WiFi transports only wrap that call.

Serial OTA (`src/main.cpp` `handleSerialOtaCommand`) is a different path
(`FirmwareFlasher`), not this task.

**Can it run at Home before a book is open?**

| Home heap | Routes up? | Largest | `xTaskCreate(12288)` |
|---|---|---:|---|
| Heartbeat ~52 KB / 38,900 (server not yet up) | **no** — `begin()` has not run | 38,900 | unreachable: nothing can POST |
| Home + Always server ~11.5 KB / **8,180** (post-fix measured) | yes | **8,180 < 12,288** | **fails**, no hole |
| Foreground File Transfer | yes | richer, not Home | possible, wrong activity |
| Settings `OtaUpdateActivity` | n/a | n/a | uses **16,384** `OtaWorkerTask`, not OtaWebCheck |

So on this device, in Always mode, a successful OtaWebCheck stack at idle
Home is a contradiction: either the endpoint does not exist, or the largest
block is too small to honour the create.

While the task *is* alive it also takes two 8,192-byte `esp_http_client`
buffers (`OtaUpdater.cpp:306–307` in `fetchReleaseJson`; same 8192/8192 on
the streamed path `:398–399`). Those are not 12,288 and are cleaned up with
the client. They would move **min-free**, not leave a 12,288 hole.

**Timing is conditional** — but the condition (an external POST against a
server that still has a 12.5 KB run) does not match the measured Home
heartbeats.

---

## 3. Full `xTaskCreate` census (`src/` + `lib/`)

| Task | Bytes | Where | Home before book open? | Lifetime | 12,288 hole? |
|---|---:|---|---|---|---|
| **TerminusFetch** | **12288** | `Registration.cpp:351,420` | **yes, pre-server** | transient; `vTaskDelete` | **yes** |
| **OtaWebCheckTask** | **12288** | `OtaWebCheck.cpp:18,83` | only with POST + room | transient; `vTaskDelete` | size yes; Home traces no |
| ActivityManagerRender | 8192 | `ActivityManager.cpp:67–71` | yes, both runs | permanent until manager teardown | no (same both boots) |
| bgwifi | 8192 | `BackgroundWifiService.h:52`, `.cpp:358` | only once server starts | service-scoped | 52 KB heartbeat ⇒ not live |
| OtaWorkerTask | 16384 | `OtaUpdateActivity.cpp:154` | no (Settings) | activity-scoped | wrong size + wrong screen |
| CoverThumb | 6144 | `EpubReaderActivity.cpp:119` | **no** — reader, 5 s idle, heap floors 96 KB / 64 KB (`:90–92`) | transient | wrong phase |
| TimeSyncTask | 4096 | `TimeSync.cpp:212`; also `CrossPointWebServerActivity.cpp:217–223` | yes, WiFi rising edge / `TimeSync::loop` from `main.cpp:1118` | transient; `vTaskDelete` (`TimeSync.cpp:92`) | 4,096, not 12,288 |
| SyncTask | 4096 | `KOReaderSyncActivity.cpp:236–252` | no | activity-scoped | no |
| AuthTask | 4096 | `KOReaderAuthActivity.cpp:39,80` | no | activity-scoped | no |

No `xTaskCreate` in `lib/`. No `xTaskCreatePinnedToCore` in the tree.
`KeyboardEntryActivity` does not create a task (the CLAUDE.md citation is
stale).

Permanent ESP-IDF stacks (`main` 8,192, `sys_evt` 4,096, lwIP, wifi) are
identical across reboots and cannot explain a 12,288 **delta**.

---

## 4. Non-stack allocations of 12,288

**None as a literal size.** Every `12288` / `12 * 1024` besides the two
stacks is a **threshold or cap**, not a malloc:

| Symbol | Role | Allocates 12,288? |
|---|---|---|
| `WEB_SERVER_MIN_SAFE_HEAP_BYTES = 12 * 1024` (`CrossPointWebServer.cpp:65`) | refuse to start server | no |
| `SERVER_SAFETY_FLOOR_BYTES = 12 * 1024` (`BackgroundServerPolicy.h:88`) | start/running floor | no |
| `kMaxSelectionTextBytes = 12 * 1024` (`EpubReaderActivity.h:106`) | selection cap; compared at `.cpp:2523` | no |
| markdown host test `12 * 1024` | test fixture | no |
| `charein_16_regular.h` glyph field `12288` | font metric | no |

Nearby transients, **wrong size or wrong phase**:

- Inflate shared window **32,768** (`InflateReader.cpp:10`) — the *victim*,
  not the hole. Reserved at book open when largest ≥ 32,768.
- Home cover framebuffer **48,000** (`HomeActivity.cpp:981–995`) — would
  collapse free heap, not leave it at 52 KB.
- Carousel frames **48,000** (`HomeCarouselCache.cpp:154,280`) — same.
- JPEG MCU row `MAX_MCU_HEIGHT * effectiveSrcW` = `16 * width`
  (`JpegToBmpConverter.cpp:167,656`). Equals 12,288 **only** for width 768.
  Home continue-reading draws a cached **BMP** (`HomeActivity.cpp:1680–1706`),
  so this decoder is not on the idle-Home path.
- Grayscale strip **8,000**; BW chunks **8,000**; WebDAV/file **4,096**;
  `HttpDownloader` TLS **2,048**; OTA HTTP **8,192**. None are 12,288.
- Cover-thumb bake is reader-only (`EpubReaderActivity.cpp:90–124`).

---

## 5. Why free returns and largest does not

1. At Home, pre-server, the large run is 38,900.
2. `xTaskCreate(..., 12288)` takes that run from one end:
   `38,900 − 12,288 = 26,612`.
3. While the task is live, HTTP/TLS and tiny strings allocate from the
   26,612 remainder (and elsewhere). Min-free records the nadir (run B:
   28,640).
4. Task exits, idle frees the 12,288 stack and TCB. Free heap ≈ original
   (Δ 76 B = leftover `std::string`s / TRMNL stage text).
5. A small live block now sits on the far side of where the stack was, so
   the 12,288 hole cannot coalesce with the 26,612 run. Largest stays 26,612
   for the rest of the session. No compaction on this platform.
6. Book open calls `ensureSharedWindow()` (32,768). 38,900 fits; 26,612 does
   not.

TCB (~288 B) and TLSF headers (~16 B) do not show up in the largest-block
delta because the hole that remains is the **stack payload** the allocator
put back as one free block; the TCB typically sits in a different, smaller
bin.

---

## 6. What to look for on the next failing boot (do not change firmware here)

Serial, failing vs succeeding Home heartbeat:

- `[TRMNL] Network ready; fetching before background server startup` — Home
  12,288 stack **did** allocate. This is the confirmation line.
- `[TRMNL] Deferring fetch: ... largest=9204` — later, during reading; does
  not contradict the line above.
- `[OTA]` / `OtaWebCheck` / `Resolved ... OTA metadata` **before** the Home
  heartbeat — would be required to resurrect OtaWebCheck as the culprit; the
  gate analysis says this should not occur at 52 KB Home.
- Absence of the TRMNL “Network ready” line on a 26,612 Home heartbeat would
  force a re-open (stacked 8,192+4,096, or an IDF buffer). Until then
  TerminusFetch is the 12,288-byte consumer.

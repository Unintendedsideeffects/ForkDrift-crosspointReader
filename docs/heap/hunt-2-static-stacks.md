# R2 — Static task stacks (`xTaskCreateStatic`)

**Verdict: do not do it.** The mechanism in the hypothesis is real. The trade is not.

A 12,288-byte caller-owned stack plus its TCB is **13,312 B of new `.bss`**. That number is not a guess: it is `TRMNL_FETCH_TASK_STACK + TRMNL_FETCH_TASK_OVERHEAD` (`src/features/terminus_sleep/Registration.cpp:351-354`), the same budget the firmware already uses as the contiguous-heap preflight for a 12,288 B `xTaskCreate`. Adding it to `.bss` shrinks the heap pool by that amount in every state.

Two measured states then break:

| State (post-fix, CONTEXT / `docs/HEAP_ANALYSIS.md`) | free today | largest today | after −13,312 `.bss` | Inflate window 32,768 |
|---|---:|---:|---:|---|
| Home, server not yet up (run A heartbeat) | 52,584 | 38,900 | ~39,272 / **~25,588** | **fails on the good path** |
| Home + background server | ~11,500 | ~8,180 | **~−1,812** | server cannot stay up |
| Reading, steady | ~36,500 | ~15,860 | ~23,188 / largest also drops | window already resident |

The 32,768-byte inflate window is reserved against the Home-before-server largest block. Run A succeeds because 38,900 ≥ 32,768. Subtract 13,312 and that largest block is ~25,588. **The currently-working open becomes the failing open.** That is the opposite of a fragmentation fix.

The code has already written this conclusion down. Terminus fetch, the other 12,288 B stack, is heap-backed on purpose:

```345:350:src/features/terminus_sleep/Registration.cpp
// fetchAndPinTrmnlImage nests SdFat writes inside esp_http_client_perform's
// data callback — too deep for the 8 KB bgwifi web-handler task (overflowed on
// the first successful image download). Run it on a dedicated task with the
// stack budget OtaWebCheckTask uses for the same workload; the 12 KB is
// heap-held only for the fetch's lifetime (a static stack would pin that DRAM
// permanently for a rare operation).
```

Sharing one static buffer among mutually exclusive tasks does not change the `.bss` invoice. One 12,288 B buffer still costs 13,312 B. Two buffers would cost twice that.

---

## Hypothesis

A transient large task stack is taken from mid-heap, the task exits, both the stack and the TCB are freed, and because TLSF never compact the hole can sit between still-live neighbours for the rest of the session. Free heap returns; the largest run does not.

The two-run measurement matches that signature exactly:

- free 52,584 vs 52,508 (Δ 76 B — the 12,288 is *back* in the free total)
- largest 38,900 vs 26,612 (Δ **12,288** — the stack constant, not coalesced)
- min-free 33,776 vs 28,640 (Δ 5,136 — something was live earlier in B)

`xTaskCreate` on this ESP-IDF (Arduino-ESP32 2.0 / `espressif32 @ 6.12.0`) allocates **two** heap blocks: stack and TCB. `vTaskDelete` returns both to TLSF independently. If they are not adjacent, the leftover hole can be exactly the stack size. That is the 12,288 B delta.

The hypothesis is sound. Static stacks would prevent *that* hole. They do it by moving the 12,288 B out of the heap pool entirely, which is the cost that sinks the idea.

---

## `configSUPPORT_STATIC_ALLOCATION`

**Enabled. No Kconfig change is required.**

This firmware is `platform = espressif32 @ 6.12.0` → Arduino-ESP32 2.0.x → prebuilt C3 SDK:

- `~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32c3/sdkconfig:1221` — `CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y`
- same file `:1980` / `:1222` — `CONFIG_FREERTOS_ENABLE_STATIC_TASK_CLEAN_UP` is **not** set
- `.../esp_additions/freertos/FreeRTOSConfig.h:205-206` — `configSUPPORT_DYNAMIC_ALLOCATION 1` and `configSUPPORT_STATIC_ALLOCATION 1`

ESP-IDF already supplies idle/timer static memory. Application code may call `xTaskCreateStatic` without implementing `vApplicationGetIdleTaskMemory`. Stack depth is in **bytes**, same as `xTaskCreate` on this port (not vanilla FreeRTOS words).

`StaticTask_t` includes `struct _reent` (`configUSE_NEWLIB_REENTRANT 1`, `FreeRTOSConfig.h:202`). The project's own TCB pad is 1,024 B. Field-level size of the dummy TCB without `_reent` is ~108 B (`StaticListItem_t` 20 B × 2, name 16, TLS pointer + delete callback, mutex fields, stack-high pointer, notifications). With newlib reentrancy the working figure is **1,024 B**, matching `TRMNL_FETCH_TASK_OVERHEAD`. All `.bss` totals below use that.

A second copy of the C3 sdkconfig (`framework-arduinoespressif32-libs/esp32c3`, Arduino 3.x) also has `CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y`. It is not the platform this repo builds.

---

## Inventory — every `xTaskCreate` in `src/` and `lib/`

`lib/` has **none**. `src/` has **eleven call sites**, nine distinct tasks (KOReader auth has two sites on alternate paths; they cannot both run).

ESP-IDF `xTaskCreate` stack argument is bytes. TCB is a second heap allocation, budgeted here at 1,024 B + two ~16 B TLSF headers while the task is alive (~1,056 B extra on top of the stack).

| # | Task | File:line | Stack B | TCB+hdr (heap, live) | Feature flag | Lifetime | Who starts it | Who deletes it |
|---|---|---|---:|---:|---|---|---|---|
| 1 | `ActivityManagerRender` | `src/activities/ActivityManager.cpp:67-72` | **8,192** | ~1,056 | always | **Permanent** (boot `begin()` → process death) | `ActivityManager::begin` | only if `begin` is undone |
| 2 | `bgwifi` | `src/network/background/BackgroundWifiService.cpp:358` ; size `BackgroundWifiService.h:52` | **8,192** | ~1,056 | `ENABLE_BACKGROUND_SERVER*` | **Activity / service** — up while Always/on-charge server runs; torn down when an activity `blocksBackgroundServer()` | `spawnTask` | `stop()` → task self-deletes |
| 3 | `OtaWebCheckTask` | `src/network/ota/OtaWebCheck.cpp:18,83` | **12,288** | ~1,056 | `ENABLE_OTA_UPDATES=1` (`platformio.ini:130`) | **Transient one-shot** | `POST /api/ota/check` from the **running** web server (`src/features/ota_updates/Registration.cpp:22-41`) | `vTaskDelete(nullptr)` at end of `otaWebCheckTask` |
| 4 | `TerminusFetch` | `src/features/terminus_sleep/Registration.cpp:351,420` | **12,288** | ~1,056 (explicitly gated as 13,312 contiguous) | `ENABLE_TERMINUS_SLEEP=1` (`platformio.ini:121`) | **Transient one-shot** | `onBackgroundNetworkReady` / recycle-then-fetch (`Registration.cpp:466-509`) | `vTaskDelete(nullptr)` |
| 5 | `OtaWorkerTask` | `src/activities/settings/OtaUpdateActivity.cpp:154` | **16,384** | ~1,056 | `ENABLE_OTA_UPDATES=1` | **Activity-scoped** (`onEnter`→`onExit`) | entering Check-for-Updates | `onExit` requests exit; trampoline `vTaskDelete` (`:122`) |
| 6 | `TimeSyncTask` (background) | `src/util/TimeSync.cpp:212` | **4,096** | ~1,056 | `ENABLE_WIFI_CLOCK=1` (`platformio.ini:142`, default in `FeatureFlags.h:357-358`) | **Transient one-shot**, retried on WiFi rising edge / 15 min | `TimeSync::loop` while STA is up | `vTaskDelete(nullptr)` (`:92`) |
| 7 | `TimeSyncTask` (web-server activity) | `src/activities/network/CrossPointWebServerActivity.cpp:217-223` | **4,096** | ~1,056 | same | **Transient one-shot** | after WifiSelection succeeds in File Transfer | `vTaskDelete(nullptr)` |
| 8 | `SyncTask` | `src/activities/reader/KOReaderSyncActivity.cpp:236-252` | **4,096** | ~1,056 | `ENABLE_KOREADER_SYNC=1` (`platformio.ini:115`) | **Activity-scoped one-shot** | only the already-connected `onEnter` path; the wifi-selection path runs `performSync()` on the **main** task (`:70-95`) | `vTaskDelete(nullptr)`; `onExit` waits |
| 9 | `AuthTask` | `src/activities/settings/KOReaderAuthActivity.cpp:39-46` and `:80-89` | **4,096** | ~1,056 | `ENABLE_KOREADER_SYNC=1` | **Activity-scoped one-shot** | already-connected `onEnter` **or** `onWifiSelectionComplete`, guarded against a second create (`:26-28`) | `vTaskDelete(nullptr)`; `onExit` waits |
| 10 | `CoverThumb` | `src/activities/reader/EpubReaderActivity.cpp:119` | **6,144** | ~1,056 | EPUB reader | **Transient one-shot**, once per open if thumbs missing (`pendingCoverThumbBake_` set `:447`, cleared after one spawn `:845`) | `queueCoverThumbBakeIfIdle` after 5 s idle | `vTaskDelete(nullptr)` |

No `xTaskCreateStatic` / `xTaskCreatePinnedToCore` in `src/` or `lib/`.

SDK / framework tasks (Arduino loop 8,192, TCP/IP 2,560, sys_evt, idle, timer) are **not** in this inventory. They are permanent, already in `docs/heap/linker-analysis.md` §5.3, and are not the 12,288 B hole.

### Which transients are large enough to matter

On this heap the allocations that can split a 32,768 B run are the ones **≥ ~8 KB** that are allocated and then freed.

| Task | Stack | Large enough? | Can it be the measured 12,288 hole? |
|---|---:|---|---|
| `OtaWebCheckTask` | 12,288 | **Yes** | **Yes — only remaining 12,288 constant after TRMNL is ruled out** |
| `TerminusFetch` | 12,288 | **Yes** | **No on this device.** CONTEXT: it logs `Deferring fetch: no room for the 13312 B task stack (largest=9204)` and never allocates. The comment at `Registration.cpp:348-350` already rejected a static stack. |
| `OtaWorkerTask` | 16,384 | Yes, bigger | No — Δ was 12,288 not 16,384. Also `blocksBackgroundServer()` (`OtaUpdateActivity.h:63`), so it does not run at the Home-before-server heartbeat unless the user went through Settings → OTA this boot. |
| `bgwifi` | 8,192 | Yes if freed | Permanent while the Always server is up. At the 52 KB heartbeat the server is **not** up, so a start-then-stop of `bgwifi` this boot could leave an 8,192 hole, not 12,288. |
| `ActivityManagerRender` | 8,192 | Occupies, does not free | Permanent. Not a hole. |
| `CoverThumb` | 6,144 | Marginal | No (wrong size). See cheaper bug below. |
| `TimeSyncTask` ×2 | 4,096 | No for a 12,288 hole | Plausible contributor to the **min-free** Δ 5,136 (4,096 + TCB). Not the largest-block Δ. |
| KOReader `SyncTask` / `AuthTask` | 4,096 | No | Activity-scoped, not on the book-open path from Home. |

**The only transient that is both large enough and not already ruled out is `OtaWebCheckTask`.** It is spawned from the background web server, so it runs *alongside* `bgwifi`, not instead of it.

### CoverThumb: a 6,144 B hole that does not need static stacks

`queueCoverThumbBakeIfIdle` calls `xTaskCreate(..., 6144, ...)` **before** the 96,000 / 64,000 heap gate. The gate lives *inside* the task (`EpubReaderActivity.cpp:143-154`). Reading steady-state is ~36,500 free / 15,860 largest, so the task always takes the defer branch — after paying for the stack. One allocate-and-free of 6,144 B per book that still needs thumbs.

The fix is to run the existing gate **before** `xTaskCreate`. `.bss` cost: 0. Not this task's job to implement; recorded so it is not "solved" by pinning 6,144 B of DRAM.

---

## Mutual exclusivity

```
Permanent, always:
  ActivityManagerRender 8192

Home + Always/on-charge server (the 11.5 KB state):
  bgwifi 8192          ──┐
  OtaWebCheck 12288    ──┼── CONCURRENT (POST /api/ota/check is a handler on that server)
  TimeSync 4096        ──┘  CONCURRENT (WiFi rising edge / 15 min)

TerminusFetch 12288:
  Designed exclusive with serving: recycle/stop server, fetch, then start server
  (`Registration.cpp:466-509`). Timeout path starts the server anyway while
  fetch still runs (`:486-491`) — overlap is possible but rare.
  Gated off this device when largest < 13,312.

Reader (bgwifi down; Epub/TXT/XTC/Markdown all block the server):
  CoverThumb 6144
  KOReader SyncTask 4096  (reader typically on the activity stack → still blocking)

Settings / OTA / File Transfer (all `blocksBackgroundServer() == true`):
  OtaWorker 16384          exclusive with bgwifi
  AuthTask 4096            Settings is on the stack → bgwifi down
  Web-server-activity TimeSync 4096  (File Transfer; bgwifi down)
```

**A single shared 12,288 B buffer can legally serve:** `OtaWebCheck` **or** `TerminusFetch` **or** `TimeSync` **or** `CoverThumb` **or** KOReader sync/auth, **one at a time**, with a mutex and the idle-task TCB-reuse wait.

**It cannot serve:**

- `bgwifi` at the same time as `OtaWebCheck` — that is the actual Home path that creates the 12,288 B hole.
- `OtaWorker` without growing the buffer to 16,384 B (then `.bss` is 17,408 B and boot largest falls to ~21.5 KB).
- `ActivityManagerRender` — it is alive for the whole process.

So "several are network tasks that cannot run concurrently" is **half true**. The 12,288 B offender *must* run concurrently with the 8,192 B `bgwifi` stack. Sharing does not absorb `OtaWebCheck` into `bgwifi`. Putting the TLS work back onto `bgwifi` was already tried: Terminus overflowed that 8,192 B stack (`Registration.cpp:345-347`). `HEAP_ANALYSIS.md:286-288` records that shrinking `bgwifi` 8,192 → 4,096 is **not** low risk for the same reason.

---

## Byte figures both ways

TCB for `.bss` = 1,024 B. No TLSF headers on static objects.

### Cost of the design that would actually close the 12,288 hole

One static worker: `StackType_t stack[12288 / sizeof(StackType_t)]` + `StaticTask_t tcb`.

| | Today (dynamic, transient) | After static 12,288 + TCB |
|---|---|---|
| `.bss` | 128,244 | 128,244 + **13,312 = 141,556** |
| Heap pool | ~179.5 KB | ~179.5 KB − 13,312 ≈ **166.2 KB** |
| Home + server free | ~11,500 | ~11,500 − 13,312 = **−1,812** |
| Home + server largest | ~8,180 | n/a (server would fail its own start gate: `startMinFreeBytes(8192)` = 8,192+16,336+12,288+4,096 = **40,912** free, `BackgroundServerPolicy.h:87-91` + `.cpp:17-18`) |
| Boot Home largest (run A) | 38,900 | ~38,900 − 13,312 = **~25,588** |
| Inflate reserve 32,768 | succeeds (38,900) | **fails every open** |
| Fragmentation avoided | — | the 12,288 B *hole* never appears, because that RAM never enters the heap |

**Fragmentation avoided, in the successful sense of the hypothesis:** up to 12,288 B of largest-block, and only in sessions that actually ran `OtaWebCheck` (or a future TRMNL fetch that passed the 13,312 gate). Run A, which never paid that hole, would lose ~13,312 B of largest-block anyway, and then fail the inflate reservation.

That is a negative expected value: you tax every boot to protect a subset of boots, and the tax is larger than the inflate window's remaining margin (38,900 − 32,768 = 6,132 B). 13,312 > 6,132.

### Smaller static options (still not the 12,288 hole)

| Design | `.bss` | Home+server free | Boot largest vs 32,768 | Closes 12,288 hole? |
|---|---:|---:|---|---|
| Static `ActivityManagerRender` 8,192+TCB | 9,216 | **wash** (already resident) | ~wash | No. Permanent occupancy is not a hole. |
| Static `bgwifi` 8,192+TCB | 9,216 | **wash while serving**; reading pays 9,216 that is currently reclaimed | ~wash at boot | No. And reading is where the inflate window lives. |
| Static TimeSync 4,096+TCB | 5,120 | ~11,500 − 5,120 = **6,380** | 38,900 − 5,120 = **33,780** (inflate still fits, 1 KB of margin) | No (wrong size). Tightens Always-server to near death. |
| Shared 16,384 to include OtaWorker | 17,408 | −5,908 | ~21,492 | Yes, and kills inflate on every boot. |
| Two dedicated 12,288 buffers (Ota + TRMNL) | 26,624 | absurd | ~12,276 | Yes, and the device cannot boot to Home. |

### What a "win" would have to look like

To keep inflate (32,768) on the good path, `.bss` growth must stay under the current largest-block margin:

38,900 − 32,768 = **6,132 B**.

A static stack + TCB of 12,288 + 1,024 = 13,312 **exceeds that margin by 7,180 B**. There is no 12 KB static design that preserves the inflate reservation.

Home+server has 11,500 free. Any new `.bss` ≥ 11,500 makes the default idle state unstartable for the Always server.

---

## If it were implemented anyway (so the next agent does not have to rediscover the traps)

This is a sketch of the *wrong* fix, not a recommendation.

1. One file, e.g. `src/network/StaticNetworkWorker.{h,cpp}`: `alignas(16) static StackType_t stack[12288 / sizeof(StackType_t)]`, `static StaticTask_t tcb`, a mutex, a `TaskHandle_t`, a "busy" flag.
2. `xTaskCreateStatic(fn, name, 12288, param, prio, stack, &tcb)`.
3. After `vTaskDelete`, **do not** reuse `tcb` until the idle task has dropped it from the termination list (`eTaskGetState` / notify-before-delete). `CONFIG_FREERTOS_ENABLE_STATIC_TASK_CLEAN_UP` is unset; idle still walks static TCBs.
4. Callers: `OtaWebCheck::start`, `startFetchTask`, optionally `TimeSync` / CoverThumb / KOReader. **Not** `bgwifi`, **not** `OtaWorker` unless the buffer grows to 16,384.
5. Reuse is exclusive. `OtaWebCheck` + `TerminusFetch` overlap on the timeout path must become a hard refuse, not a second `xTaskCreate`.

`.bss` is still 13,312 B. The arithmetic above does not care how clean the wrapper is.

Heap-allocating the same buffer once at boot and never freeing it is **worse** than `.bss`: it plants a 12,288 B island in the heap, which is the inflate-window failure mode CONTEXT already records (freeing that window was tried and reverted because the hole came back 12 bytes too small).

---

## Cheaper alternatives that are not static stacks

None of these are this task's implementation scope. They are listed so static stacks are not used as a proxy for them.

1. **Do not spawn `OtaWebCheckTask` from the background server.** `OtaUpdateActivity` already has a 16,384 B worker and already `blocksBackgroundServer()`. An OTA check from Settings does not punch a 12,288 B hole into the Home-before-open heap. `.bss`: 0. Product change: `/api/ota/check` either 404s or queues a flag for the settings activity.
2. **Gate CoverThumb before `xTaskCreate`** (`EpubReaderActivity.cpp:119` vs `:143-154`). Stops a 6,144 B allocate-and-free that currently always defers. `.bss`: 0.
3. **Leave TRMNL heap-backed and gated.** Already deferred on this device. Already documented as "do not pin 12 KB for a rare operation."

---

## Recommendation

**Do not convert any of these tasks to `xTaskCreateStatic` for fragmentation.**

- The API is available (`configSUPPORT_STATIC_ALLOCATION=1`). That is not the question.
- The 12,288 B hole is real and matches `OtaWebCheckTask`. Static allocation would prevent that hole.
- It would also shrink the heap pool by 13,312 B, which is more than the 6,132 B of largest-block margin the inflate window needs on the good path, and more than the 11,500 B Home+server has left.
- Sharing one buffer among exclusive network one-shots does not reduce that 13,312 B. `OtaWebCheck` is not exclusive with `bgwifi`.
- Permanent stacks (`ActivityManagerRender`, `bgwifi` while serving) are capacity-neutral to move to `.bss` and do **not** address the hypothesis (they are not freed). Moving `bgwifi` to `.bss` would *worsen* reading, where that 8,192 B is currently returned to the heap.

The trade is not worth it. Leave the 12,288 B stacks on the heap, keep the Terminus comment as the design record, and spend the next change on not spawning `OtaWebCheckTask` at Home — or on not creating `CoverThumb` when the 96 KB / 64 KB gate cannot pass — not on pinning 13 KB of DRAM that Home does not have.

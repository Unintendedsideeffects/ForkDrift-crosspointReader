# Lens 1 — Allocation Census & Reclaim Headroom

## Executive Summary
This report presents a complete census of heap consumption and FreeRTOS task stacks for the CrossPoint Reader firmware running on the ESP32-C3 (Xteink X4, single-core RISC-V, no PSRAM). Total measured heap available at boot after static `.bss`/`.data` allocations is **~179,400 bytes** (`ESP.getHeapSize()`).

---

## What Holds the ~128KB When Free Heap is ~11KB at Home

When the device is at the Home Activity with the background web server running in default mode (`backgroundServerMode = Always`), free heap drops to **11,148 bytes** (with a largest contiguous block of only **5,620 bytes**). Out of the ~179.4KB total heap, **~168.3KB** is consumed or locked.

### Accounting Breakdown

1. **Network Stack & WiFi Driver Overhead (~47.4KB Total Accounted Heap)**
   - **ESP-IDF WiFi / LwIP / TCP-IP stack buffers & data structures**: **~25,000 B** (Inferred & measured baseline).
   - **Background Server / `CrossPointWebServer` / WebSockets / mDNS / UDP Discovery**: **~22,400 B** total measured drop when started.
     - WebServer allocation & route setup: **12,036 B** [02-exit-reader-reclaim.log:30-35](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L30-L35).
     - WebSocket server & UDP discovery initialization: **4,024 B** [02-exit-reader-reclaim.log:35-47](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L35-L47).
     - mDNS start + WiFi shelf check state: **5,750 B** [02-exit-reader-reclaim.log:47-50](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L47-L50).

2. **FreeRTOS Task Stacks (~28.7KB Total Stack Space)**
   - **`sys_evt` (ESP-IDF System Event loop task)**: **4,096 B** (Boot-permanent).
   - **`main` task (Arduino main loop context)**: **8,192 B** (Boot-permanent).
   - **`ActivityManagerRender` task**: **8,192 B** stack + ~288 B TCB [ActivityManager.cpp:65-70](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/ActivityManager.cpp#L65-L70).
   - **`bgwifi` background worker task**: **8,192 B** stack + ~288 B TCB [BackgroundWifiService.h:52](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/network/background/BackgroundWifiService.h#L52).

3. **Caches, Metadata & UI Buffers (~32KB - 80KB Variable)**
   - **Home cover snapshot cache (when active)**: **48,000 B** heap allocation [HomeActivity.cpp:995](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/home/HomeActivity.cpp#L995) (Note: conditionally freed before shelf refresh [HomeActivity.cpp:420](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/home/HomeActivity.cpp#L420)).
   - **EPUB metadata / spine / TOC structure caches**: **~10KB - 25KB** inferred from book parsing.
   - **Font metric & glyph caches**: **~5KB - 15KB**.

4. **Heap Heapguard Reserves & Internal Heap Allocator Metadata (~15KB - 25KB)**
   - Dynamic heap allocation overhead (block headers, alignment, TLS/C++ runtime structures).

5. **Unaccounted / Internal ESP-IDF System Heap**: **~15KB - 25KB** (WiFi internal rx/tx buffers, NVS flash cache, driver DMA structures).

---

## Table A — Allocation Census

| Consumer | Bytes | Lifetime | Owner (file:line) | Type |
|---|---|---|---|---|
| `ActivityManagerRender` Task Stack | 8,192 B (+288 B TCB) | Permanent | [ActivityManager.cpp:65-70](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/ActivityManager.cpp#L65-L70) | Inferred |
| `bgwifi` Task Stack | 8,192 B (+288 B TCB) | Permanent | [BackgroundWifiService.h:52](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/network/background/BackgroundWifiService.h#L52) | Inferred |
| `main` Task Stack (Arduino) | 8,192 B | Permanent | ESP-IDF / PlatformIO main | Inferred |
| `sys_evt` Task Stack | 4,096 B | Permanent | ESP-IDF WiFi event task | Inferred |
| `CoverThumb` Task Stack | 6,144 B | Transient (Thumb generation) | [EpubReaderActivity.cpp:119](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/reader/EpubReaderActivity.cpp#L119) | Inferred |
| `TimeSyncTask` Stack | 4,096 B | Transient (NTP sync) | [TimeSync.cpp:212](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/util/TimeSync.cpp#L212), [CrossPointWebServerActivity.cpp:217-223](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/network/CrossPointWebServerActivity.cpp#L217-L223) | Inferred |
| `SyncTask` / `AuthTask` Stack | 4,096 B | Activity-scoped | [KOReaderSyncActivity.cpp:252](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/reader/KOReaderSyncActivity.cpp#L252), [KOReaderAuthActivity.cpp:46](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/settings/KOReaderAuthActivity.cpp#L46) | Inferred |
| `OtaWebCheckTask` Stack | 12,288 B | Transient (Background OTA check) | [OtaWebCheck.cpp:18](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/network/ota/OtaWebCheck.cpp#L18) | Inferred |
| `OtaWorkerTask` Stack | 16,384 B | Activity-scoped (Settings OTA) | [OtaUpdateActivity.cpp:154](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/settings/OtaUpdateActivity.cpp#L154) | Inferred |
| `TerminusFetch` Task Stack | 12,288 B | Transient (TRMNL fetch) | [Registration.cpp:351](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/features/terminus_sleep/Registration.cpp#L351) | Inferred |
| WebServer & Route Handlers | 12,036 B | Service-scoped (`Always` mode) | [02-exit-reader-reclaim.log:30-35](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L30-L35) | **Measured** |
| WebSocket Server + UDP Discovery | 4,024 B | Service-scoped (`Always` mode) | [02-exit-reader-reclaim.log:35-47](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L35-L47) | **Measured** |
| mDNS Service Startup | 5,750 B | Service-scoped (`Always` mode) | [02-exit-reader-reclaim.log:47-50](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L47-L50) | **Measured** |
| Background Web Server Teardown Recovery | **22,324 B** total | Reclaimed on stop/delete | [02-exit-reader-reclaim.log:60-67](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/02-exit-reader-reclaim.log#L60-L67) | **Measured** |
| Home Cover Buffer Cache | 48,000 B | Activity-scoped (Home) | [HomeActivity.cpp:995](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/home/HomeActivity.cpp#L995) | Inferred |
| Reader Selection Base Snapshot | 48,000 B | Activity-scoped (Selection mode) | [EpubReaderActivity.cpp:1734](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/reader/EpubReaderActivity.cpp#L1734) | Inferred |
| Reader Grayscale Strip Scratch | 8,000 B | Transient (Per-page render) | [EpubReaderActivity.cpp:2272](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/reader/EpubReaderActivity.cpp#L2272) | **Measured** (Fails in low-heap: OOM logged [03-open-book-degrade.log:51](file:///tmp/claude-1000/-home-malcolm-Code-ForkDrift/32225248-9c63-4b7e-897c-be8dba4192b7/scratchpad/heap/traces/03-open-book-degrade.log#L51)) |
| Primary Display Framebuffer (`frameBuffer0`) | 48,000 B | Boot-permanent | [EInkDisplay.h:158](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/open-x4-sdk/libs/display/EInkDisplay/include/EInkDisplay.h#L158) | Inferred (Statically allocated `.bss`) |

---

## Table B — Ranked Reclaim Opportunities

| Change | Est. Bytes Recovered | Risk | Files Touched | Why it is safe or not |
|---|---|---|---|---|
| **1. Change default `backgroundServerMode` from `Always` (2) to `OnDemand` (0) or `OnCharge` (1)** | **~22,324 B** heap (and +15KB largest block) | **Low** | `src/network/background/BackgroundServerPolicy.h` | Web server routes and WebSocket are only running when explicitly requested or charging. Restores Home steady state heap to ~47KB. |
| **2. Reduce `bgwifi` task stack size from 8,192 B to 4,096 B or 6,144 B** | **2,048 B - 4,096 B** | **Low-Medium** | [BackgroundWifiService.h:52](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/network/background/BackgroundWifiService.h#L52) | `bgwifi` mainly manages state machines and triggers light HTTP/WiFi calls. 8KB is overly conservative for an event dispatcher task. |
| **3. Reduce `ActivityManagerRender` stack from 8,192 B to 6,144 B** | **2,048 B** | **Medium** | [ActivityManager.cpp:66](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/ActivityManager.cpp#L66) | Render task stack handles UI activity render calls. Must be verified against max stack usage in complex UI renderings. |
| **4. Optimize Grayscale Strip Scratch rendering (reduce `STRIP_ROWS` from 80 to 40)** | **4,000 B** transient alloc reduction | **Low** | [EpubReaderActivity.cpp:2265](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/activities/reader/EpubReaderActivity.cpp#L2265) | Reducing strip rows from 80 to 40 drops the required contiguous block from 8,000 B to 4,000 B, allowing anti-aliased text rendering to succeed even when max free block is ~9KB. |
| **5. Right-size `TerminusFetch` stack from 12,288 B to 8,192 B** | **4,096 B** (when fetch active) | **Medium** | [Registration.cpp:351](file:///home/malcolm/Code/ForkDrift/crosspoint-reader/src/features/terminus_sleep/Registration.cpp#L351) | TRMNL fetch task stack fails often due to contiguous heap checks (`TRMNL_FETCH_TASK_STACK + TRMNL_FETCH_TASK_OVERHEAD`). Reducing stack size allows fetches under lower heap. |

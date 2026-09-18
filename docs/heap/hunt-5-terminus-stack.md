# R5 — How small can the Terminus fetch task stack safely be?

**Measured verdict (2026-08-30): complete HTTP fetches used at most 2,256 bytes on the old 12 KB task, 2,352 bytes on the shipped 8 KB fallback (on-charge), and 2,560 bytes on bgwifi's resident 8 KB stack (Always). Always-mode is device-accepted at 5,632 bytes free; on-charge fallback is device-accepted at 5,840 bytes free. HTTPS is still unmeasured.**

This device’s fetch is **plain HTTP** to `http://192.168.86.25:2300`. TLS/mbedTLS is **not** on that path. The product still allows HTTPS (`https://trmnl.com` is the default base URL). A stack sized only for this LAN box will crash a cloud user.

The durable developer log captured multiple successful full fetches at 2,244-2,256 bytes used, including image validation, the SD rename, and settings persistence. A manifest-only HTTP 500 used 2,168 bytes; the extra full-path cost was therefore only 88 bytes after un-nesting.

---

## 1. Worst-case call path of `fetchAndPinTrmnlImage()`

Entry: `terminusFetchTask` (`Registration.cpp:382`) → `fetchAndPinTrmnlImage()` (`:247`).

### 1.1 Sequence (two HTTP sessions, then SD metadata)

| Step | Where | What sits on *this* task’s stack |
|---|---|---|
| Snapshot credentials | `:251-254` | four `std::string` (SSO object ~24–32 B each; payloads on heap) |
| Allow-list the base URL | `:260-264` + `TerminusApi.cpp:49-64` | small |
| `TimeSync::ensureTrustedClock()` | `:269`, again `:186` | cheap if clock valid (`TimeSync.cpp:167-171`). If not, SNTP runs *here* (the dedicated TimeSync task is 4096 B and is a different task). Sequential with HTTP, not nested. |
| Manifest GET `/api/display` | `:273-307` | `BoundedManifestSink` (body is heap, cap 16 KiB at `:47`), `esp_http_client_config_t` (~160–180 B), `esp_http_client_perform` |
| Parse JSON | `TerminusApi.cpp:76-96` | ArduinoJson 7 `JsonDocument` is a small stack object; the pool is **heap**. Happens *after* the first `perform` returns. |
| Image GET | `downloadVerifiedImage` `:161-244` | `VerifiedFileSink` (`:97-105`, ~40 B + `HalFile` unique_ptr), second `esp_http_client_config_t`, second `perform` |
| **Peak** | `imageEventHandler` `:133-156` inside `perform` | HTTP client + lwIP recv + `SpiBusMutex::Guard` + `HalFile::write` → SdFat + SPI |
| Persist pin path | `:329-335` → `JsonSettingsIO::saveSettings` `:94-100` | `JsonDocument` + Arduino `String` (heap). After `cleanup`. Sequential, not nested with HTTP. |

The two HTTP clients are **not** live together. The manifest `config` is in a nested block (`:272-307`) that ends before `downloadVerifiedImage`.

### 1.2 The nested image write (this is the depth that overflowed 8 KB)

```
terminusFetchTask
  fetchAndPinTrmnlImage          // credential strings + DisplayManifest still live
    downloadVerifiedImage
      esp_http_client_perform    // IDF 4.4, Arduino-ESP32 2.0 / espressif32 6.12.0
        connect (TCP, not SSL)   // host is a dotted IPv4 — no getaddrinfo
        send headers
        fetch headers / http_parser
        transport_read / lwip_recv
          HTTP_EVENT_ON_DATA
            imageEventHandler                    Registration.cpp:133
              memcpy 4-byte magic                :139-145, trivial
              SpiBusMutex::Guard                 include/SpiBusMutex.h:11
              HalFile::write                     HalStorage.cpp:245
                HalStorage::StorageLock          mutex
                FsFile::write                    greiman/SdFat ^2.3.1
                  FAT cache is a 512 B *member*, not a stack array
                  SPI transfer of the HTTP chunk (up to buffer_size)
```

`config.buffer_size` / `buffer_size_tx` = `TRMNL_HTTP_BUFFER_BYTES` = **2048** (`Registration.cpp:48, :193-194, :279-280`). Those two buffers are allocated by `esp_http_client_init` on the **heap**, not the task stack.

### 1.3 Sizeable stack frames (file:line)

Anything that is actually large on the stack:

| Object | File:line | Stack bytes | Notes |
|---|---|---:|---|
| `logPrintf` format buffer | `lib/Logging/Logging.cpp:18,115` `MAX_ENTRY_LEN=256` | **256** | Only when a `LOG_*` fires. Not in `imageEventHandler`. |
| `esp_http_client_config_t` | `Registration.cpp:188,274` | ~160–180 | Pointer-heavy; one live at a time. |
| `fetchAndPinTrmnlImage` strings | `:251-273, :323` | ~200–250 | `std::string` objects; payloads heap. |
| `BoundedManifestSink` | `:69-95, :273` | ~32 | `body_` heap, capped at 16 KiB (`:47`). |
| `VerifiedFileSink` | `:97-105, :167` | ~40 | `HalFile` is a unique_ptr. |
| ArduinoJson `JsonDocument` | `TerminusApi.cpp:77`, `JsonSettingsIO.cpp:95` | ~16–32 | ArduinoJson **7.4.2** (`platformio.ini:93`) pools on the heap. |

What is **not** on this stack, despite looking scary:

- HTTP rx/tx 2×2048 — heap (`esp_http_client_init`).
- Manifest body up to 16 KiB — `std::string` heap.
- Settings JSON `String` — heap.
- mbedTLS 16 KiB content buffers (`CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384`, sdkconfig `:1459`) — heap, and **only if the URL is https**. `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y` (`:1456`) puts MPI on the heap too.
- SdFat 512 B sector cache — `FatVolume` member.

The overflow is **call-chain depth**, not a 4 KB local array. That is why a static byte-count of “our” frames (~1 KB) under-predicts the peak: IDF `esp_http_client_perform` + `lwip_recv` + SdFat write + two mutexes sit on top of it.

FreeRTOS on this SDK is canary-checked (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y`, sdkconfig `:1210`). Overflow is a panic, not a return code.

---

## 2. HTTP vs HTTPS on this device

**This device: plain HTTP. TLS is not in the peak.**

Serial (multiple heap walks, e.g. `scratchpad/heap/unwedge2/serial.log:34`):

```
[TRMNL] Polling http://192.168.86.25:2300/api/display (model=xteink_x4)
```

Same host is recorded as the BYOS server in `docs/FINDINGS.md` (2026-08-12T20:30Z, 2026-08-15T11:20Z). `isPrivateLanHttpUrl` (`UrlUtils.cpp:32-52`) allows `http://` only for numeric RFC1918/loopback IPv4. `192.168.86.25` matches `192.168.` (`:45`). `isAllowedRemoteUrl` (`TerminusApi.cpp:49-51`) accepts that before the HTTPS branch.

`crt_bundle_attach = arduino_esp_crt_bundle_attach` is set on **both** clients (`Registration.cpp:195, :281`). That only matters when the URL scheme is `https`. `esp_http_client` selects `HTTP_TRANSPORT_OVER_TCP` vs `OVER_SSL` from the URL. For `http://192.168.86.25:2300` the cert-bundle callback is stored and not used. No mbedTLS handshake, no 16 KiB TLS buffers, no DNS (`getaddrinfo`).

**The firmware as a product still does HTTPS.** Default base URL is `https://trmnl.com` (`TerminusApi.h:10`). Any non-LAN host must be HTTPS (`TerminusApi.cpp:53-55`). Cloud Terminus, or a LAN host served as HTTPS, takes the TLS path: `esp_http_client` → `esp_transport_ssl` → mbedTLS, *and* the same nested SdFat write. HttpDownloader’s comment (`HttpDownloader.cpp:21-28`) is about **heap** for 16 KiB mbedTLS buffers (~40 KB contiguous in the old Arduino `WiFiClientSecure` path), not about task stack — but the handshake **call chain** is extra stack on top of HTTP+SdFat.

Terminus is stricter than HttpDownloader: it attaches the CRT bundle. HttpDownloader deliberately does not (`HttpDownloader.h:8-10`). Cert-bundle verify is deeper than `setInsecure()`.

OtaWebCheck’s `12288` (`OtaWebCheck.cpp:18`) is the HTTPS-JSON budget **without** nested SdFat. Terminus copied that number “for the same workload” (`Registration.cpp:345-350`). It is not the same workload: Terminus adds SdFat inside the callback, and on this device it subtracts TLS.

---

## 3. Estimate and recommended size

### What is actually known

| Bound | Evidence |
|---|---|
| Dedicated-task HTTP+SdFat **survives** `12288` | Device has completed manifest→image→pin (FINDINGS 2026-08-12: ~900 ms LAN fetch). |
| `8192` **overflowed** | Comment at `Registration.cpp:345-347`: nested SdFat inside `perform`, “8 KB bgwifi web-handler task”, first successful image download. Canary would panic. |
| The 8 KB that overflowed was **bgwifi**, not a fresh 8 KB task | `BackgroundWifiService.h:52` `TASK_STACK = 8192`. The comment says **web-handler** task. Route handlers run on that stack (`HEAP_ANALYSIS.md:286-288`). `POST /api/terminus/test` used to be the natural place to run a fetch; today it only sets a flag (`Registration.cpp:660-674`). |
| Full HTTP fetch high-water | 2,244-2,256 B used; 10,032-10,044 B free on the former 12,288 B task. Durable `/crosspoint-debug.log`, 2026-08-30. |

A dedicated `TerminusFetch` task starts empty at `terminusFetchTask`. bgwifi at overflow time already had `taskEntry` → `run` and, if the fetch was invoked from a handler, `WebServer::handleClient` + Arduino `String` URI/header objects. Those frames are **not** on the dedicated task. So `8192` overflowing on bgwifi does **not** prove a dedicated `8192` task overflows.

It also does not prove the opposite. Font downloads run the same nest (`HttpDownloader.cpp:59-76, :377`, HTTPS, no CRT bundle) on the Arduino **loop** task, which is also `8192` (`sdkconfig:230`, `cores/esp32/main.cpp:36-38,71`). That path working is existence proof that HTTP-or-insecure-HTTPS + nested SdFat *can* fit in 8 KB on a relatively empty task. It is not a margin measurement, and Terminus’s CRT-bundle HTTPS path is deeper than FontDownload.

### Static estimate, this device (HTTP, dedicated task, still nested)

| Layer | Bytes (order of magnitude) |
|---|---:|
| Our frames + strings + `config_t` | 400–700 |
| `esp_http_client_perform` + parser + transport | 1.5–3 KB |
| `lwip_recv` on the calling task | 400–800 |
| SdFat write + SPI + two mutexes | 500–1500 |
| Canary / ISR headroom | 256–512 |
| **Guessed peak** | **~5.5–8.5 KB** |

The upper end of that range is why I will not recommend `8192` while the nest remains. If the true peak is 7.8 KB, `8192` has ~400 B of canary margin and will pass for weeks then panic on a FAT allocate + SPI path that is 500 B deeper.

### Recommendation

| Situation | Stack | Why |
|---|---:|---|
| **Fallback task after measured un-nested fetch** | **8192** | Maximum measured use 2,256 B leaves 5,936 B for HTTPS/logging variance, well above the 2,048 B policy. |
| **HTTP-only firmware, still nested, no high-water** | **10240** | 2 KB below today, 2 KB above the size that overflowed on a *different* task. Margin: ~1.5–4.5 KB against the guess above. I would not go lower without a number. |
| **After un-nest + measured free ≥ 2048 B on HTTP *and* HTTPS** | then **8192** or **eliminate the task** | See §5. |
| **8192 today, nested** | **no** | Repeats a known crash class. A stack overflow is not a deferred fetch. |

Margin policy: keep **≥ 2048 B** of high-water free (Espressif’s usual “one unexpected frame + logging + FAT path”). 1024 B is the absolute floor I would tolerate after a measured peak, not before.

Hole arithmetic if you only resize: `xTaskCreate` still punches a hole of whatever the stack constant is. `12288 → 10240` saves 2 KB of hole; `12288 → 8192` saves 4 KB. The measured Home delta is 12,288. A 10 KB stack still splits a 38,900 run into a 28,660 remainder, which **still fails** the 32,768 inflate window. **Shrinking this stack without dropping below ~6 KB does not save the inflate reservation.** The value of a smaller stack is (a) the preflight `canAllocate(stack+1024)` passing more often (today 13312 vs reading largest 9204), and (b) a smaller hole for *other* later allocations. It is not a fix for run B.

`TRMNL_FETCH_TASK_OVERHEAD = 1024` (`:354`) is TCB + TLSF headers so the preflight does not pass only for `xTaskCreate` to fail. Keep it if the stack stays heap-backed.

---

## 4. Instrumentation to prove it

**Already in tree** (`Registration.cpp:386-400`):

```
uxTaskGetStackHighWaterMark(nullptr)   // words; code multiplies by sizeof(StackType_t)
LOG_INF("TRMNL", "Fetch task stack: %u of %u bytes used, %u free (%.0f%% margin)", ...)
```

Read it **before** `vTaskDelete` — that is the right place; it is the minimum remaining stack over the whole task, including `saveToFile`.

The on-device capture now exists on both HTTP paths. Dedicated 8 KB task peaked at 2,352 B used / 5,840 B free (on-charge recycle). Always-mode logged `bgwifi pre-server stack: 2560 of 8192 bytes used, 5632 free`. Extra `taskEntry`/`run` frames cost 208–304 B versus the dedicated task. Remaining capture is HTTPS.

To prove a shrink:

1. Flash a build that contains `:396-400`.
2. Force a **successful image** fetch (not just the 276-byte manifest). Log line to grep: `Fetch task stack:`.
3. Record **HTTP LAN** (`http://192.168.86.25:2300`) and, separately, **HTTPS** (`https://trmnl.com` or any https `image_url`). One number cannot cover both.
4. Do not shrink unless **both** show `free ≥ 2048`. If only HTTP is measured, do not shrink the constant that HTTPS users share.
5. Optional, one extra `LOG_INF` inside `imageEventHandler` on the first `ON_DATA` (high-water then vs at task end) tells you whether the peak is the nested write or `saveToFile`. Not required for a go/no-go.
6. Include the URL scheme in the existing log (`https` vs `http`) so a mixed fleet is not averaged.

ESP-IDF: high-water is in **words**. The existing multiply is correct on C3 (`StackType_t` = 4 B). `used = 12288 - freeBytes`.

If you shrink, keep the canary (`CHECK_STACKOVERFLOW_CANARY` is already on). A watchpoint at end-of-stack (`CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK`) is a debug-only extra, not a substitute for the log.

---

## 5. Alternatives to shrinking (preferred)

### 5.1 Un-nest the SdFat write — this is the real fix

The 8 KB overflow is specifically “SdFat writes inside `esp_http_client_perform`’s data callback” (`:345-347`). That additive peak is a design choice, not a law of HTTP.

`esp_http_client_open` / `fetch_headers` / `read` into the existing 2 KB heap buffer, **then** `HalFile::write` after `read` returns:

```
while ((n = esp_http_client_read(client, heapBuf, 2048)) > 0) {
  SpiBusMutex::Guard guard;
  sink.file.write(heapBuf, n);   // HTTP/lwIP frames already popped
}
```

Peaks become `max(HTTP, SdFat)`, not `HTTP + SdFat`. HTTPS becomes `max(TLS+HTTP, SdFat)`. The 2 KB buffer already exists (`buffer_size`); this does not add a heap allocation.

HttpDownloader (`:48-76`) uses the same nest. Un-nesting Terminus first is enough for this hole; copying the pattern to HttpDownloader is a later, separate win for the loop task.

After un-nest, re-measure. HTTP-only peak should land in the 4–6 KB band. Then `8192` on a dedicated task is plausible with ≥2 KB margin, **or** the dedicated task can go away (below).

### 5.2 Run on an existing task instead of `xTaskCreate(12288)`

A new 12,288 B stack is what leaves the Home hole (`hunt-1`). Using a stack that is **already allocated** does not punch a 12 KB hole.

| Existing task | Stack | Can it host the fetch? |
|---|---:|---|
| **bgwifi, while serving** | 8192 | **No.** This is the overflow. `handleClient` + HTTP + SdFat. `HEAP_ANALYSIS.md` already forbids shrinking bgwifi for this reason. |
| **bgwifi, pre-server window** | 8192 | **Yes, after un-nest.** `onBackgroundNetworkReady` runs *before* `CrossPointWebServer` exists. Stack is `taskEntry` → `run` → hook → fetch, not a route handler. Device-accepted 2026-08-30: 2,560 B used / 5,632 B free. |
| **Arduino loop** | 8192 | Sleep/view already block on the fetch (`main.cpp:442`, `TrmnlViewActivity.cpp:201` → `startTrmnlFetchAndWait`). Those callers could call `fetchAndPinTrmnlImage()` on loopTask. FontDownload already does nested HTTPS+SdFat here. **Do not** bounce the Home pre-server fetch onto loopTask: on-charge mode reaches the same hook from the main loop and FINDINGS 2026-08-12 already recorded a 2 s UI stall from waiting there. |
| TimeSync | 4096 | No. |
| ActivityManagerRender | 8192 | No. Permanent render task; do not put HTTP on it. |
| OtaWebCheck / OtaWorker | 12288 / 16384 | Wrong lifetime, HTTPS-shaped, not on the Home pre-server path. Sharing a static 12 KB buffer was rejected in hunt-2 (`.bss` tax). |

**Implemented structural outcome:** un-nest, then run the Always-mode Home fetch **synchronously on bgwifi in the pre-server window**. Hole = 0 on the product's default path. The on-charge dispatcher still uses an 8 KB fallback task because its hook runs on the interactive main loop; making that path synchronous previously froze input. The recycle-to-fetch dance stays because it also frees heap for HTTP/TLS buffers.

Do not put the nested write back onto bgwifi “to save a task” without un-nesting. That is how 8 KB died.

### 5.3 What not to do

- Do not `xTaskCreateStatic` the 12 KB (hunt-2: 13,312 B of `.bss` kills the inflate reservation on the *good* path).
- Do not shrink to 8192 “because FontDownload works on loopTask” while the nest and CRT bundle remain.
- Do not size only for this LAN HTTP box while `isAllowedRemoteUrl` still accepts `https://`.

---

## Bottom line

- **This device, this path: HTTP, no mbedTLS.** Verified. TLS would dominate; it does not, here.
- **Measured un-nested dedicated-task requirement, HTTP:** 2,256 B on the old 12,288 B task; **2,352 B used / 5,840 B free** on the shipped 8,192 B fallback after on-charge recycle (2026-08-30T13:40Z).
- **Measured Always-path requirement, HTTP:** 2,560 B used / 5,632 B free on bgwifi's 8,192 B stack after flash to `192.168.86.51` (2026-08-30T13:15Z). Compile-time peak is this number.
- **Fallback ship size: `8192`.** Always-path margin is 5,632 B; on-charge dedicated-task margin is 5,840 B. The compile-time contract refuses any future size below measured peak + 2,048 B.
- **Default Always-mode fix:** no new task allocation. The fetch uses bgwifi's resident stack before the route table exists, so it does not punch a hole ahead of the 32,768-byte inflate reservation. On the Always acceptance boot, post-fetch largest was 9,204 B — 12 B under the 9,216 B fallback-task preflight — so a shrunk dedicated task would still have deferred.
- **On-charge fallback:** async 8 KB task, HTTP accepted. `BWS` waits on `backgroundStartupDeferred` rather than running the fetch on the interactive loop.
- **Remaining proof:** repeat against HTTPS when a cloud endpoint is available. This LAN BYOS box speaks HTTP only.

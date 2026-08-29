# R3 — Background-server diet

Home idle **was** the worst state on the device (~11,500 free, ~8,180 largest)
because Always-mode kept a full `CrossPointWebServer` resident — ~84
`server->on()` heap handlers, mDNS, and WebSockets. Teardown recovered a
measured **22,324 B**. This note is the attack plan against the WebServer
library that is actually linked.

**Landed 2026-08-29** (P0, P1, P1b, P2c, step-7 start budget). Device walk on
`192.168.86.51`: after UDP `"hello"`, Home+Always sat at **29,392–36,640 free /
17,396 largest**; 35/35 `/api/status` HTTP 200; wifi stayed `Connected`; no
`Retry scheduled (low heap)` flap. First flash after P0 still refused port 80 at
37,348 free because `SERVER_STARTUP_BYTES` still charged 11,832 for the deleted
routes; retune 16,336 → 4,504 is what let Always start. Follow-on: AA no longer
allocates an 8 KB scratch (framebuffer loan + BW re-paint). Host tests compile
`mountAll()` only under `CROSSPOINT_HOST_BUILD` / `SIMULATOR`; firmware has one
live registration path. Background `begin()` defers mDNS/WS until the first UDP
hello; File Transfer / Calibre still start those at activity entry.

The sections below are the original design. Do not re-derive the 11,500 / 8,180
starting point as current firmware truth.

Not in scope: cutting `BackgroundWifiService::TASK_STACK` (8192). Route handlers run on that task (`BackgroundWifiService.h:51-52`).

---

## 0. What library this is

This is **not** ESPAsyncWebServer, not a custom HTTP stack, and not ESP-IDF `httpd`.

| Piece | Actual dependency |
|---|---|
| HTTP | Arduino-ESP32 `WebServer` (`#include <WebServer.h>`), Ivan Grokhotkov / Hristo Gochkov, shipped in `framework-arduinoespressif32/libraries/WebServer` |
| WebSocket | `links2004/WebSockets @ 2.7.3` (`platformio.ini:97`), `WebSocketsServer` on port 81 |
| mDNS | Arduino `ESPmDNS` wrapping ESP-IDF `mdns_init()` / `mdns_free()`. Task stack **4096** (`CONFIG_MDNS_TASK_STACK_SIZE` in the esp32c3 sdkconfig) |
| Discovery | `NetworkUDP`/`WiFiUDP` bound to **8134**, payload `"hello"` → `"crosspoint (on <hostname>);81"` |

`platformio.ini` lib_deps do not list a WebServer package because it is part of the Arduino-ESP32 core.

### API facts that constrain every proposal below

All line numbers are the installed core at `~/.platformio/packages/framework-arduinoespressif32/libraries/WebServer/src/`.

1. **`THandlerFunction` is `std::function<void(void)>`** (`WebServer.h:101`). Every `server->on(...)` takes that type.
2. **`on()` always heap-allocates a `FunctionRequestHandler`** with bare `new` (`WebServer.cpp:252-253`):
   ```
   _addRequestHandler(new FunctionRequestHandler(fn, ufn, uri, method));
   ```
   No nothrow. OOM in the library aborts. There is no `on_P`, no function-pointer overload, no `removeHandler`.
3. **Each `FunctionRequestHandler` clones the URI onto the heap** (`RequestHandlersImpl.h:13-19`): `_uri(uri.clone())` → `new Uri(_uri)` (`Uri.h:18-19`), and `Uri` holds an Arduino `String`. So every `server->on("/api/...", ...)` is **three live heap blocks**: the handler object, the cloned `Uri`, the `String` buffer. Plus ~16 B TLSF header each.
4. **The 3-arg `on(uri, method, fn)` still stores a second `std::function`**: it forwards `_fileUploadHandler` as `ufn` (`WebServer.cpp:248-249`). Upload routes (`/upload`, `/api/fonts/upload`) pass a real second lambda.
5. **`addHandler(RequestHandler*)` is the escape hatch** (`WebServer.h:105`, `WebServer.cpp:256-268`). Appends to a singly-linked list (`_firstHandler` / `_lastHandler`). WebDAV already uses this (`CrossPointWebServer.cpp:270-272`). The destructor walks the list and `delete`s every node (`WebServer.cpp:91-100`), so a handler passed to `addHandler` is owned by `WebServer`.
6. **`begin()` does not register routes.** It `close()`s the listen socket and calls `WiFiServer::begin()` (`WebServer.cpp:103-107`). Handler list is independent of listen state. **Routes may be added before or after `begin()`.** `begin()` does not clear handlers.
7. **Handler selection happens in `_parseRequest`, before `_handleRequest`** (`Parsing.cpp:126-130`). First `canHandle(method, uri) == true` wins. Upload/raw bodies are then streamed to `_currentHandler` during the same parse (`Parsing.cpp:176-197`, `327-328`). If no handler is installed yet, the body is consumed without an upload callback.
8. **`onNotFound` is a single `std::function` slot**, not a `FunctionRequestHandler` (`WebServer.cpp:631-632, 646-648`). It runs only when `_currentHandler` is null or `handle()` returned false. It cannot re-run the handler walk for the current request.
9. **There is no API to inject an already-accepted `WiFiClient` into `WebServer`.** `handleClient()` only takes clients from its own `WiFiServer::available()` (`WebServer.cpp:276-277`).

### What CLAUDE.md's std::function warning actually means here

CLAUDE.md (`Template and std::function Bloat`): `std::function<void()>` adds **~2–4 KB of flash per unique signature** and heap-allocates its closure when the capture does not fit the small-buffer optimisation.

All 51 core routes share **one** signature (`void()`). The `[this]` capture is a pointer and fits SBO, so **the lambdas are not a 2–4 KB *heap* cost per route**. The measured 12,028 B is the `FunctionRequestHandler` + cloned `Uri` + `String` buffer + TLSF headers, not 51 heap-allocated closures.

The flash warning is still real and still an argument for a function-pointer table (one less reason to keep `std::function` in this TU). Do not budget 2–4 KB heap per handler; that overstates the prize and sends the implementation at the wrong object.

### Per-handler size, reconciled with the 12,028 B measurement

A `FunctionRequestHandler` is roughly:

- handler object (vptr, `_next`, `std::vector<String> pathArgs`, two `std::function`, `Uri*`, `HTTPMethod`) ≈ 60–80 B + 16 B TLSF
- cloned `Uri` ≈ 16–24 B + 16 B TLSF
- URI `String` buffer ≈ 16–48 B + 16 B TLSF

≈ **140–180 B / `server->on()`**.

`mountRoutes()` at `CrossPointWebServer.cpp:326-411` is **51 `server->on()` calls** on the default profile (`ENABLE_REMOTE_CONTROL=1`, `ENABLE_WIFI_CLOCK=1`), plus `onNotFound`, plus `core::WebRouteRegistry::mountAll(server.get())` at line 364. Default-profile plugins that also call `server->on()`:

| Feature | Routes | File |
|---|---:|---|
| terminus | 5 | `src/features/terminus_sleep/Registration.cpp:561-683` |
| health | 1 | `src/network/server/StaticHandlers.cpp:24-26` |
| anki | 8 | `src/features/anki/Registration.cpp:32-` |
| wallpaper | 1 | `src/features/web_wallpaper/Registration.cpp` |
| pokemon-wallpaper | 1 | `src/features/web_pokemon_wallpaper/Registration.cpp` |
| pokemon-party page | 1 | `src/features/web_pokemon_party/Registration.cpp` |
| remote-keyboard | 4 | `src/features/remote_keyboard_input/Registration.cpp` |
| ota | 2 | `src/features/ota_updates/Registration.cpp` |
| pokemon-party API | 6 | `src/features/pokemon_party/Registration.cpp` |
| wifi-setup | 4 | `src/features/web_wifi_setup/Registration.cpp` |

≈ 33 plugin routes + 51 core ≈ **84 `FunctionRequestHandler`s**. 12,028 / 84 ≈ **143 B each**, which matches the breakdown above. (If some `shouldRegister()` returned false on the measured boot, the per-handler figure is a bit higher and the count a bit lower; the 12,028 B is the number that matters.)

They are allocated in one tight loop at start, so they occupy a near-contiguous ~12 KB run. Replacing them is a **contiguity** win, not just a free-heap win. Home's binding number is largest-block (8,180), not free (11,500).

---

## Measured starting point (do not re-derive)

From `docs/heap/lens-4-budget.md` trace `02` and `BackgroundServerPolicy.h:79-83`. CONTEXT's "WS + mDNS + UDP ~6,000" slightly overlaps the begin() bucket; use this sequence:

| Step | Δ free | What actually ran |
|---|---:|---|
| `new WebServer(port)` | **−304** | `CrossPointWebServer.cpp:234` |
| `mountRoutes()` | **−12,028** | 51 + plugin `server->on()` |
| rest of `begin()` (logged as "after server.begin()") | **−3,992** | WebDAV `addHandler` + `collectHeaders` + `WiFiServer::begin` + `new WebSocketsServer` + `wsServer->begin()` + `udp.begin(8134)` (`:265-323`) |
| `MDNS.begin(hostname)` + settle | **~−5,750** | `BackgroundWifiService.cpp:211-216` (and the on-charge twin at `BackgroundWebServer.cpp:167`) |
| **Teardown stop+delete** | **+22,324** | `CrossPointWebServer::stop` then `delete` |

304 + 12,028 + 3,992 + 5,750 ≈ 22,074, inside the teardown figure. The 22,324 does **not** include the 8,192 `bgwifi` stack (still alive until the task exits) and does **not** include WiFi STA/LwIP (~25 KB, radio stays up).

`CrossPointWebServer` itself also constructs `FontUploadState` with `buffer.resize(4096)` in the member ctor (`CrossPointWebServer.h:197`). That 4 KB is paid at `new CrossPointWebServer()`, **before** the "WebServer allocation" log, so it sits in the pre-begin drop, not in the 12,028. It is still Always-mode idle cost.

`WEB_SERVER_MIN_SAFE_HEAP_BYTES` is 12,288 and is checked after routes and after WS+UDP (`CrossPointWebServer.cpp:65, 259-262, 300-313`). It does not include mDNS. Always-mode then sits at 11.5 KB, under that floor, because mDNS is started in the wifi service *after* `begin()` returns.

---

## Priority order

Do these in order. Later items assume earlier ones, or are dominated by them.

### P0 — Replace `server->on()` with one `RequestHandler` and a flash dispatch table

**This is the whole 12,028 B, permanently, including while a client is connected.** Lazy registration of the same `FunctionRequestHandler`s cannot beat it. Do this first; treat lazy `on()` as a fallback only if P0 is rejected.

#### How, on this API

Do not call `server->on()`. Call `server->addHandler(handler)` once.

```cpp
struct Route {
  const char* uri;                                 // string literal → flash
  HTTPMethod method;
  void (*fn)(CrossPointWebServer*);                // or member-fn pointer
  void (*ufn)(CrossPointWebServer*);               // nullptr if not an upload route
};

class TableHandler : public RequestHandler {
  // canHandle: strcmp uri + method against the table (HTTP_ANY if needed)
  // handle:    call matched->fn(owner)
  // canUpload: matched && matched->ufn
  // upload:    matched->ufn(owner)
  // canRaw:    false (WebDAV owns raw PUT)
};
```

In `mountRoutes()`:

```cpp
auto* table = new (std::nothrow) TableHandler(this, kRoutes, kRouteCount);
if (!table) { LOG_ERR("WEB", "OOM: TableHandler"); return; }
server->addHandler(table);          // WebServer owns it, destructor deletes it
core::WebRouteRegistry::mountAll(server.get());  // see below
server->onNotFound([this] { handleNotFound(); }); // keep: AP captive-portal
#if CROSSPOINT_HAS_NETWORKUDP
server->addHandler(new (std::nothrow) WebDAVHandler());  // already this pattern
#endif
```

`canHandle` must use `strcmp` / `equals` on `uri.c_str()` against the flash literal. Do not copy URIs into Arduino `String`s at registration. `RequestHandler::pathArgs` stays empty; none of these routes use `UriBraces` / `UriRegex`.

Upload routes today:

```cpp
server->on("/upload", HTTP_POST, [this] { handleUploadPost(); }, [this] { handleUpload(); });
server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
```

Put those two `ufn` pointers in the table. `FunctionRequestHandler::canUpload` requires POST + a non-empty `_ufn` (`RequestHandlersImpl.h:33-37`); match that.

**Plugins.** `WebRouteRegistry` is already a function-pointer registry (`WebRouteRegistry.h:12-16`) — then each `mountRoutes(WebServer*)` throws the table away and calls `server->on()` anyway. Change `mountRoutes` to *fill the flash/static table* (or a second `WebRouteSpec[]` with `reserve`/`count`), and have `TableHandler` walk core routes then plugin specs. Keep `shouldRegister()` as the feature gate. Do not leave plugins on `server->on()` or you keep ~33 × 143 B ≈ **4.7 KB**.

**Matching order.** First `canHandle` wins. Current order is core `on()` calls, then `mountAll`, then WebDAV. Preserve that: table (core + plugins) first, WebDAV last. WebDAV's `canHandle` returns true for `HTTP_GET` (`WebDAVHandler.cpp:28-46`); it must stay after the table or it swallows `/` and `/api/*`.

**`onNotFound`.** Keep as the one remaining `std::function`. Needed for AP captive-portal redirects (`StaticHandlers.cpp:54-65`). Cost: one SBO `std::function` inside `WebServer`, not a `FunctionRequestHandler`.

**Do not fork `WebServer.cpp`.** `addHandler` is public and sufficient. Patching the core to accept function pointers would fight every PlatformIO package bump.

#### Bytes saved

- **~12,028 B heap** at idle *and* while serving, minus one `TableHandler` (~80–150 B) minus TLSF header.
- **Net ≈ 11.8–12.0 KB.**
- **Largest-block:** the 51–84 sequential handler allocations are the best candidate on the device for coalescing into one run. Expected largest-block lift at Home is **in the same ballpark as the 12 KB**, i.e. 8,180 → ~20 KB, *if* nothing else sits in the middle of that run. Verify; do not claim 12 KB largest until measured.
- **Flash:** the table is `const Route[]` in `.rodata`. Removing dozens of `std::function` constructions should slightly *shrink* the binary. Flash is 91.9% full; this is one of the few heap plays that does not grow it.
- **Allocator headers:** 84 handlers × ~3 blocks × 16 B ≈ **4 KB of TLSF headers** currently taxed against `getHeapSize()`. One handler returns most of that to the reported pool.

#### What breaks

- Nothing user-visible if the table is a 1:1 transcription of `mountRoutes` + plugin `server->on()` lists, including method and order.
- `Uri` path-args (`server.pathArg(i)`) — unused by these routes today; do not add brace URIs to the table without extending `TableHandler`.
- Host tests that construct a real `WebServer` and call `on()` — `test/host_server` mounts production routes. Update those to the table or they silently 404.
- Bare `new` inside the Arduino `on()` path goes away for our routes; `TableHandler` must use `nothrow` as WebDAV already does.

#### How to verify

1. Keep the existing `[MEM]` logs in `begin()` (`CrossPointWebServer.cpp:230, 246, 257, 323`). **`after route setup` must drop from −12,028 to a few hundred bytes.**
2. `scripts/device_walk.py` heap verb, Home + Always server: free and largest vs current 11,500 / 8,180.
3. Contract smoke (see `docs/android-app-contract.md` and `docs/webserver-endpoints.md`):
   - GET `/`, `/api/status`, `/health`, `/files`, `/settings`
   - POST `/upload` (multipart) and `/api/fonts/upload` — these are the two `ufn` paths
   - POST `/api/settings`, `/api/wifi`, `/delete`
   - WebDAV PROPFIND (handler still after the table)
   - AP mode: unrecognised URL still 302s via `onNotFound`
4. Plugin pages: `/plugins/terminus`, `/plugins/anki`, `/plugins/wallpaper` on a default-profile build.
5. Confirm flash did not grow (`firmware_size_history.py` / map).

---

### P1 — Defer mDNS until something actually needs `.local`

**~5,750 B**, almost all one 4096-byte `mdns` task stack plus TCB and `mdns_init` bookkeeping. The 1,460 B `packet.8427` in `.bss` (`docs/heap/linker-analysis.md`) is static and **not** in this 5,750; killing mDNS does not return those 1,460 B to heap.

Today `MDNS.begin(hostname)` runs unconditionally once the HTTP server is up:

- Always mode: `BackgroundWifiService.cpp:211-216`
- On-charge: `BackgroundWebServer.cpp:167-172`
- File Transfer activity: `CrossPointWebServerActivity.cpp:232, 296` (keep this one)

Android discovery (`docs/android-app-contract.md` §1–§2) is **UDP 8134 first**, and also probes `http://crosspoint.local/api/status` with a 2 s timeout. The contract already tells Android to prefer the UDP hostname over a hardcoded `crosspoint.local`.

#### How

Keep UDP 8134 always-on (it is inside the 3,992 B begin-bucket, cheap compared with mDNS). Start mDNS on the first of:

1. UDP `"hello"` received (`handleClient` already parses this at `CrossPointWebServer.cpp:492-508`), or
2. File Transfer / Calibre activity (already does), or
3. an explicit setting if product wants Always-`.local`.

Do **not** start mDNS from the first HTTP request: a browser that typed `http://hostname.local/` never gets a SYN if mDNS is down.

Wire it in `BackgroundWifiService::run` / `BackgroundWebServer::startServer`: delete the eager `MDNS.begin`. Add `ensureMdns()` next to the UDP hello reply (same task, so no extra stack). `MDNS.end()` on stop stays as it is.

#### Bytes saved

- **~5,750 B free** at Home idle when no app has scanned and File Transfer is not open.
- **Largest-block:** the mdns task stack is one contiguous 4096 B allocation. Deferring it is a clean largest-block win of ~4 KB, plus whatever `mdns_init` scattered.
- Combined with P0: Home idle theoretically ~11,500+12,000+5,750 ≈ **29 KB free**, largest hopefully ~8,180+12,000+4,096 ≈ **24 KB**. Both numbers are "if they coalesce"; measure.

#### What breaks

- Typing `http://<hostname>.local/` in a desktop browser, with Always-mode and no prior UDP scan, fails until something else starts mDNS. IP URL and UDP-discovered IP still work.
- Android's *secondary* `crosspoint.local` probe (2 s) races with "start mDNS on first hello" if both fire at once. UDP reply still fills the device list. Worst case: the `.local` row is missing for that scan; the UDP row is present. Acceptable if documented in the android contract §2 ("mDNS is on-demand in Always mode").
- Anything that hardcoded `crosspoint.local` without UDP (the contract already says not to).

#### How to verify

1. Split the post-begin log: mDNS is currently folded into "settle". Log `ESP.getFreeHeap()` / `ESP.getMaxAllocHeap()` immediately before and after `MDNS.begin`. Confirm Δ ≈ 5,750.
2. Home idle with Always, no LAN client: mDNS must **not** start; heap holds the 5,750.
3. ForkDriftApp scan: UDP `"hello"` → reply still has `;81`; then mDNS may start; `.local` probe may now succeed on a *second* scan.
4. File Transfer activity: `http://<hostname>.local/` still works (eager path unchanged).
5. `MDNS.end()` still runs on reader entry / stop, so the 5,750 does not leak across a book open.

---

### P1b — Make WebSocket on-demand (same priority band as mDNS, smaller, sharper edge)

Of the **3,992 B** "after server.begin()" bucket, most is `WebSocketsServer` + its listen socket + client slots, not `WiFiServer::begin()` on port 80. Policy comment called this 4,200 together with UDP (`BackgroundServerPolicy.h:82`). Lens-1 called WS+UDP **4,024 B**. UDP 8134 is a small fraction; treat **~3.5–4.0 KB as WS-dominated**.

Android fast upload is WebSocket on port 81 (`android-app-contract.md` transport table; reply string advertises `wsPort`). HTTP `/upload` is the fallback.

#### How, given this API

`WebSocketsServer` is a separate listen socket. It is **not** tied to `WebServer::begin()`. You can:

```cpp
server->begin();          // port 80 only
udp.begin(LOCAL_UDP_PORT);
// do not construct wsServer yet
```

and in `handleClient()`, after `server->handleClient()`:

```cpp
if (!wsServer && /* trigger */) {
  wsServer.reset(new (std::nothrow) WebSocketsServer(wsPort));
  ...
}
if (wsServer) wsServer->loop();
```

Triggers that actually work:

- First UDP `"hello"` (app is on the LAN and about to open a WS), or
- First HTTP request to a page that loads the WS client (`/` / `/files`), or
- First connection attempt to port 81.

The third has the same inject problem as HTTP: a listen socket that is not `WebSocketsServer` cannot be handed over. So either keep a cheap `WiFiServer(81)` (still costs a PCB) or start `WebSocketsServer` on UDP hello — the app always sends hello before connecting.

**Do not advertise port 81 in the UDP reply until WS is up**, or advertise it and accept that the first WS connect can lose a race with `ensureWs()`. Safer: on UDP hello, start WS *before* sending the reply. Cost: one hello pays ~4 KB, then the app's WS connect hits a live socket. Idle Home with no app: 4 KB saved.

#### Bytes saved

- **~3.5–4.0 KB** at idle with no app / no File Transfer.
- Serving / first app scan: paid in full (no saving while the app is connected).

#### What breaks

- A client that connects to `ws://ip:81` **without** a prior UDP hello or HTTP hit (rare; not the Android path). First SYN to 81 is refused until WS exists.
- Foreground File Transfer should still start WS in `begin()` (user is there to upload). Gate the deferral on "background instance", not `CrossPointWebServer::begin()` globally, or File Transfer uploads regress.
- `getWsUploadStatus()` / in-progress upload teardown already null-checks `wsServer`.

#### How to verify

1. Split the current single "after server.begin()" log into: after `server->begin()`, after `addHandler(WebDAV)`, after `udp.begin`, after `wsServer->begin()`. Put numbers on each.
2. Home idle, no app: no `WebSocket server started` log; heap keeps the 4 KB.
3. Android: scan → hello → WS up → binary upload.
4. Browser on `/files` using HTTP multipart `/upload` (no WS): must keep working with WS still down.
5. File Transfer activity: WS up at start, huge-file upload unchanged.

---

### P2 — Lazy / deferred *route registration* (only if P0 is rejected)

The prompt asked for this explicitly. On **this** `WebServer`, it is a trap. Documented so it is not implemented as "register 5 `on()`s, `onNotFound` mounts the rest".

#### What people think will work

```cpp
server->on("/", HTTP_GET, ...);
server->on("/api/status", HTTP_GET, ...);
server->onNotFound([this] {
  if (!restMounted) { mountRest(); restMounted = true; }
  handleNotFound(); // or "retry"
});
server->begin();
```

#### What the library actually does

1. `_parseRequest` picks `_currentHandler` **before** any user callback (`Parsing.cpp:126-130`).
2. For POST/PUT, the body is read in the same function and offered to `_currentHandler->upload` / `raw`. If the real `/upload` handler is not in the list yet, **the first upload's body is discarded**.
3. `_handleRequest` then calls `_notFoundHandler()` if nothing matched (`WebServer.cpp:646-648`). That callback **cannot** re-walk the list for this request. Newly added `on()` handlers only affect the *next* connection.
4. You may legally `on()` after `begin()` (fact 6). That only helps request N+1.
5. There is no `removeHandler`. Once the rest are mounted, they stay until `WebServer` destruction. Idle saving lasts until the first non-bootstrap URI — including a crawler, Android `/api/status`, or `/health`.

Android GET `/api/status` is in the discovery path (`android-app-contract.md` §3). If `/api/status` is in the bootstrap set, a device scan expands nothing (good) but you also kept that handler forever (fine). If it is *not* in the bootstrap set, the first status GET 404s or is served from `onNotFound` by hand.

#### A version that would work (and why it is P0 in disguise)

Install **one** catch-all `RequestHandler` whose `canHandle` is always true, and dispatch inside it from a flash table. First GET, first POST, first upload all work because `canUpload` is implemented on that same object. That *is* P0. Doing it and also calling `server->on()` later doubles the handlers and wastes the 12 KB again.

Bootstrap-set `on()` + delayed `on()` is strictly worse: you pay FunctionRequestHandler cost for the bootstrap set forever, pay the rest on first use, and the first POST to a deferred URI is wrong.

#### Bytes saved (if someone still does the naive version)

- Idle, no HTTP at all: **~12,028 − bootstrap**. A honest bootstrap is `/`, `/api/status`, `/health`, `/upload`+ufn, `onNotFound` ≈ 5–7 handlers ≈ **~1 KB**, saving **~11 KB** until first hit of a deferred URI.
- First Android scan that also GETs anything not in the bootstrap set: saving gone for the rest of the session. Heap cannot compact; those 80 handlers then sit in whatever holes exist *after* Home has been running — **worse fragmentation than allocating them at start in one run**.
- This last point is why naive lazy is not just "P0 later": **it can make largest-block worse than eager `on()`**.

#### What breaks

- First POST `/upload` or `/api/fonts/upload` if those `on()` calls are deferred.
- First request to a deferred URI: 404 or a hand-rolled dispatch in `onNotFound` that must duplicate `_handleRequest`.
- AP captive-portal probes (`/generate_204`, `/hotspot-detect.html`) currently hit `onNotFound` and 302. If `onNotFound` grows "mount rest then 302", every phone probe expands the full table — Always-mode AP would never stay lean.
- WebDAV: `canHandle` is method-wide. Must be present before the first PROPFIND. Deferring WebDAV is OK (P2b); deferring it *behind* `onNotFound` means the first PROPFIND 404s.

#### How to verify (if implemented)

Do not call it done after a GET `/` test. Must catch:

1. First-ever POST multipart `/upload` after boot, no prior GET.
2. First-ever WebDAV PROPFIND.
3. Android scan + `/api/status`.
4. Heap *after* first deferred URI: confirm you did not scatter 80 small blocks into a fragmented Home heap (largest-block before vs after that request).

---

### P2b — Defer WebDAV + `collectHeaders`

`collectHeaders` of 6 DAV names plus `Authorization` allocates `RequestArgument[7]` (`WebServer.cpp:588-596`). `WebDAVHandler` is one object (already `addHandler`, already nothrow). Together they are a few hundred bytes of the 3,992 B bucket, not a headline number.

Defer until the first non-GET that is not in the table (OPTIONS/PROPFIND/PUT/MKCOL/…). `TableHandler::canHandle` returns false for those; today they fall through to WebDAV. For deferral, `onNotFound` cannot see them if you only install WebDAV later — same first-request loss as P2.

Practical approach: leave WebDAV registered. It is already the good pattern. Come back only if P0+P1 still leave Home under the CSS/AA floors.

---

### P2c — Lazy `FontUploadState` 4 KB buffer

`FontUploadState::FontUploadState() { buffer.resize(BUFFER_SIZE); }` with `BUFFER_SIZE = 4096` (`CrossPointWebServer.h:193-197`). Paid for every Always-mode server, used only during a font POST.

Construct with an empty `vector`, `reserve(4096)` on the first `handleFontUploadData()` chunk, `shrink`/`clear` on upload end. `std::vector` on a cold path is allowed (heap-discipline: not a render path).

**Bytes:** **4,096 B** idle, every background start. Independent of P0. Do it in the same patch series; it is local.

**Breaks:** first font-upload chunk must handle OOM (`makeUniqueNoThrow` / `reserve` can throw on this STL — use `try` is disabled; `reserve` with `-fno-exceptions` may abort). Prefer `buffer` as `unique_ptr<uint8_t[]>` via `makeUniqueNoThrow<uint8_t[]>(4096)` at first use, null-check, 503.

**Verify:** `[MEM]` around `new CrossPointWebServer()` (add a log there; it does not exist today). Fonts page upload still installs a face.

---

### P3 — Start the *whole* server only when a client connects

Asked explicitly. **You cannot, not with this `WebServer`, without losing the first HTTP request.**

#### Why

- Detecting "a client connected" on TCP requires a listen socket in `LISTEN`.
- The listen socket *is* `WebServer`'s `WiFiServer`. `handleClient` will not take a `WiFiClient` you accepted elsewhere (fact 9).
- Sequence "accept → construct `CrossPointWebServer` → `begin()`" drops the accepted client; `begin()` opens a *new* listen socket. Browsers sometimes retry GET; they do not retry POST. Android `/api/status` has a 2 s timeout.
- UDP hello can wake the HTTP server (app scan is intentional). That still leaves browsers that type `http://<ip>/` with no UDP: first SYN must hit an already-listening port 80.

#### What *can* be done (thin listener, not zero listener)

Idle Always-mode keeps:

- WiFi STA (already paid, ~25 KB, out of the 22,324)
- `bgwifi` task 8192 (out of scope)
- `WebServer(80)` + `begin()` (**304 + listen PCB**, a few hundred B)
- UDP 8134 (needed for discovery; ~part of today's 4 KB WS+UDP, keep this, drop WS via P1b)
- P0 table handler (~150 B) — so "thin" is actually fully capable HTTP

That is **not** a 22 KB saving. After P0+P1+P1b the residual Always-mode HTTP cost is `WebServer` + listen + UDP + table ≈ **well under 1–2 KB** plus the wifi task. The 22,324 is already gone. A further "construct CrossPointWebServer on first SYN" saves the `CrossPointWebServer` object (font buffer, upload `String`s, `unique_ptr`s) — a few KB at most once P2c is done — and costs a lost first connection.

**Do not build a second listen/accept state machine to shave the last 304 B.**

UDP-as-trigger for the *current* 22 KB monster (no P0) would work for the app and fail for IP-typed browsers. That is a product choice, and it still cannot hand the first TCP client to `WebServer`. Reject as the primary diet.

#### Bytes saved

- Full deferral of `CrossPointWebServer`: **~22 KB** until first UDP or first HTTP, **if** you accept a lost first HTTP or you keep a dummy `WiFiServer(80)` anyway (which collapses toward the thin-listener design).
- Realistic residual after P0–P1b: **<2 KB** to fight for. Not worth the first-request bugs.

#### What breaks

- First browser GET to the IP: connection reset / timeout.
- Captive-portal probes in AP mode: must have a live HTTP listener; Always-STA is the diet target, but `CrossPointWebServer` is shared with AP File Transfer.
- Android 2 s `/api/status` if the wake path is slower than 2 s (route setup today is the slow/heavy part; P0 makes `begin()` cheap, which *removes* the original reason to defer construction).

#### How to verify

Only if product still wants it: tcpdump/Wireshark on first GET after boot with a cold server; confirm whether the library can respond on that same TCP connection (it cannot). That experiment is the gate; if the first GET is lost, stop.

---

## Combined target and what not to do

| Step | Idle Home Δ (approx) | Still saved while app connected? | Risk |
|---|---:|---|---|
| P0 dispatch table | **+12,028** free, likely **+~12 KB** largest | yes | low if table is 1:1 |
| P1 defer mDNS | **+5,750** | no (comes back on hello / File Transfer) | medium (`.local`) |
| P1b defer WS | **+~4,000** | no | medium (first WS) |
| P2c lazy font buffer | **+4,096** | yes (until a font POST) | low |
| P2 lazy `on()` | do not do | — | high (first POST, worse frag) |
| P3 whole-server-on-connect | do not do as primary | — | high (lost first HTTP) |
| Cut `bgwifi` stack | **forbidden** | — | handlers run there |

P0 + P1 + P1b + P2c is the diet: **~26 KB** at idle Home with no LAN client, **~16 KB** still resident while the app is connected (P0 + P2c). Home would move from 11,500 / 8,180 toward the mid-20s / ~20 KB largest if the freed runs coalesce. That is the region where CSS parse, AA scratch (8,000), and the Terminus 13,312 stack stop being structurally impossible.

**Measured 2026-08-29, after the bytes moved:** Home+Always after UDP hello is 29–36 KB free / 17.4 KB largest (not mid-20s — better than the coalesce guess). CSS whole-file skip is 32 KB critical; AA scratch still fails. `SERVER_STARTUP_BYTES` retuned 16,336 → 4,504. Always’s start gate is ~29 KB, not 40,912. First flash after P0 at 37 KB free still `ECONNREFUSED` until that retune.

After the bytes move, retune `BackgroundServerPolicy::SERVER_STARTUP_BYTES` (today 16,336, measured 2026-07-30). The start gate is `taskStack + 16336 + 12288 + 4096 = 40,912` (`lens-4-budget.md` §3.6). A cheaper start is how Always-mode *restarts* after a book (currently the restart at free ≈ 33,216 is below that gate and "unresolved"). Do not lower the gate until the new startup is measured with the split logs.

### Explicit non-goals

- **Do not switch to ESPAsyncWebServer.** More heap, more tasks, flash cost, different API.
- **Do not fork Arduino `WebServer.cpp`** to add a function-pointer `on()`. `addHandler` is enough.
- **Do not cut `TASK_STACK`.**
- **Do not change `backgroundServerMode` default** as a substitute for this diet. HEAP_ANALYSIS rec 5 is a product decision; this note is the engineering lever that keeps Always.
- **Do not persist CSS / section cache built under the old 11 KB Home** — already a HEAP_ANALYSIS item; out of this diet's diff, still true after.

---

## Suggested implementation sequence (for whoever picks this up)

1. Add split `[MEM]` logs in `begin()` (WebServer / routes / DAV / `server->begin()` / WS / UDP) and around `MDNS.begin`. **Measure a baseline on device before changing allocation.** The 12,028 / 3,992 / 5,750 split is from one trace; confirm on current HEAD.
2. P0 table for the 51 core routes only. Plugin `on()` still live. Confirm `after route setup` falls by ~51×143 ≈ 7 KB. Ship if that is already enough largest-block for AA.
3. Point `WebRouteRegistry` at the table. Rest of the 12,028.
4. P2c font buffer.
5. P1 mDNS on UDP hello; File Transfer unchanged.
6. P1b WS on UDP hello, background instance only.
7. Re-measure `SERVER_STARTUP_BYTES`, retune the start gate, update `BackgroundServerPolicy.h` comment block. **Done 2026-08-29:** 16,336 → 4,504; Always starts at measured Home idle.

Each step is independently revertible and independently measurable against the `[MEM]` lines. That is the reviewability constraint.

---

## API cheat-sheet for the implementer

```
WebServer::on(uri, method, fn)           → FunctionRequestHandler + Uri clone   AVOID
WebServer::on(uri, method, fn, ufn)      → same, two std::function              AVOID
WebServer::addHandler(RequestHandler*)   → one object, we own the matching      USE
WebServer::onNotFound(fn)                → one std::function, no Uri            KEEP for AP
WebServer::begin()                       → listen only, handlers untouched      CALL after addHandler
WebServer::handleClient()                → own WiFiServer only                  cannot inject
Parsing picks handler before body        → upload routes must exist first       P0 table, not lazy on()
ESPmDNS::begin / mdns_init               → 4096-stack task                      P1 defer
WebSocketsServer(port)                   → separate listen on 81                P1b defer
NetworkUDP::begin(8134)                  → Android discovery                    KEEP
```

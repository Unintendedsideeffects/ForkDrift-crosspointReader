# Host HTTP shim prototype

This prototype runs a tiny host-only HTTP server that exposes the same Arduino-style callback surface the firmware handlers use, but translates real TCP requests through `cpp-httplib` on Linux/macOS host builds.

**Wired routes:** `GET /api/settings/raw` plus the shared file-management routes mounted by
`src/network/CoreWebRoutes.cpp` and `src/network/FileRoutes.cpp`.

The host harness still avoids device lifecycle work such as Wi-Fi, UDP discovery, WebSockets, and WebDAV, but the mounted routes now live in production-owned `src/network` files instead of test-only adapters.

## Build and run

```bash
bash test/run_host_server.sh
```

The script builds `build/host_server/HostServer`, launches it on a random high port, curls the settings and file-management routes, and exits non-zero if the smoke test fails.

## Running integration tests

The Python integration test harness exercises all file-management routes defined in the OpenAPI spec. It launches the binary, populates a temporary storage directory, and performs end-to-end assertions.

```bash
# Assumes the binary is already built by run_host_server.sh
python3 test/host_server/integration_tests.py --binary build/host_server/HostServer
```

Expected output:
```text
test_01_list_files (__main__.HostServerTest) ... ok
test_02_download_file (__main__.HostServerTest) ... ok
test_03_mkdir (__main__.HostServerTest) ... ok
...
Ran 8 tests in 1.234s
OK
```

If a route is not yet implemented by the server, the test will be marked as `skipped` rather than failing.

## Adding more routes later

1. Prefer adding portable route adapters under `src/network/*Routes.cpp`.
2. Mount them from `src/network/CoreWebRoutes.cpp` when the host harness should exercise them.
3. If that handler needs more Arduino `WebServer` surface, extend `test/host_server/HostWebServer.{h,cpp}` instead of inventing a parallel API.

`HostWebServer` already handles the common request/response pieces (`on`, `hasArg`, `arg`, `send`, `sendHeader`, `send_P`, `uri`, `method`). More specialized firmware paths such as upload streaming and socket-level client writes are left as explicit TODO stubs for the next step.

## Pre-ship / branch hygiene

- [ ] Flash `fork-drift` to hardware device (`uv run pio run --target upload` or OTA from Android app)
- [ ] Smoke-test with Android app after flash: Wi-Fi API, USB serial, sleep cover pin/clear
  - Android app (ForkDriftApp) is now in shape for testing: release signing, API 34 CI, custom icon, improved empty states
- [ ] Merge `fork-drift` → `master` once device smoke test passes (1173 commits ahead of master)

## Technical debt

- [ ] Hardware smoke test — `hardware-smoke-test.yml` scaffold exists in ForkDriftApp; needs self-hosted runner with device connected via USB and secrets `CROSSPOINT_DEVICE_HOST`, `CROSSPOINT_BLE_TARGET`, `CROSSPOINT_TEST_WIFI_SSID/PASSWORD`

# Plan: Boot into TRMNL only on charge

> **Status:** T1–T8 shipped. T9 hardware verification pending.
> **Architecture this plan assumed (direct OTA-pair as dual-boot pair) is superseded by `plan-trmnl-coexistence.md`.**
> The toggle, settings, i18n, splash, and asymmetry guard delivered here all carry forward unchanged. Only the *delivery mechanism for TRMNL into `app1`* and the *interaction with OTA* are now handled by the factory-partition design in the coexistence plan.

## Background

- TRMNL lives on the **other OTA partition**. Switching is `esp_ota_set_boot_partition()` + `esp_restart()` (existing manual path: `crosspoint-reader/src/activities/settings/SettingsActivity.cpp:353-360`).
- Feature is gated by `Capability::TrmnlSwitch` (`src/core/features/FeatureModules.h:41`, `src/core/features/FeatureCatalog.cpp:51`) and compile flag `ENABLE_TRMNL_SWITCH`.
- `src/features/trmnl_switch/Registration.cpp` is currently an empty stub — no boot-time logic.
- USB/charge detection: `gpio.isUsbConnected()`, already read at `src/main.cpp:398` before SD/display init.
- `BootActivity` runs at startup (`src/activities/boot_sleep/BootActivity.cpp`).

## Decisions

1. **Gating**: both compile flag (`ENABLE_TRMNL_SWITCH`) and runtime opt-in setting (`bootToTrmnlOnCharge`, default 0). Setting visible only when `Capability::TrmnlSwitch` is present.
2. **Bounce protection**: not needed. TRMNL has no counterpart "boot back to reader when unplugged" logic, so once TRMNL is booted it stays. No ping-pong risk. Skip RTC-flag complexity.
3. **Settings storage**: SD-backed JSON (Option A). Check fires after `Storage.begin()` and settings load, before `setupDisplayAndFonts()`.
4. **Visual feedback**: brief "Switching to TRMNL…" splash before `esp_restart()` so it doesn't look like a crash.

## Asymmetry note

Reader is the only firmware that knows the policy. For this to make sense, reader should be the **default boot partition** — otherwise an unplugged cold boot still lands on TRMNL. Consider a `LOG_WRN` if `esp_ota_get_running_partition()` isn't the default slot at boot, or document this in the setting's description.

## Files to change

1. `crosspoint-reader/src/CrossPointSettings.h` / `.cpp`
   - Add `uint8_t bootToTrmnlOnCharge = 0;`
   - JSON load/save in `JsonSettingsIO`.

2. `crosspoint-reader/lib/I18n/translations/english.yaml`
   - Add `STR_BOOT_TO_TRMNL_ON_CHARGE: "Boot to TRMNL when charging"`.
   - Run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`.
   - Commit YAML + `I18nKeys.h` + `I18nStrings.h`; do NOT commit `I18nStrings.cpp` (pre-commit hook enforces).

3. `crosspoint-reader/src/SettingsList.h`
   - Inside the existing `Capability::TrmnlSwitch` block (around line 241), add a `Toggle` for `STR_BOOT_TO_TRMNL_ON_CHARGE` bound to `SETTINGS.bootToTrmnlOnCharge`.

4. `crosspoint-reader/src/features/trmnl_switch/Registration.h` / `.cpp`
   - Declare `void maybeBootToTrmnl(bool usbConnectedAtBoot);`
   - Real impl under `#if ENABLE_TRMNL_SWITCH`, empty stub otherwise.
   - Logic:
     ```cpp
     if (!usbConnectedAtBoot) return;
     if (SETTINGS.bootToTrmnlOnCharge == 0) return;
     const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
     if (!next) return;
     // brief splash
     esp_ota_set_boot_partition(next);
     esp_restart();
     ```

5. `crosspoint-reader/src/main.cpp`
   - After settings have loaded (post-`Storage.begin()`) and before `setupDisplayAndFonts()`, call `features::trmnl_switch::maybeBootToTrmnl(usbConnectedAtBoot)`.
   - Function never returns on the switch path.

## Verification (on hardware)

- [ ] Setting OFF + charging at boot → reader boots normally.
- [ ] Setting ON + unplugged at boot → reader boots normally.
- [ ] Setting ON + charging at boot → TRMNL boots, splash visible.
- [ ] Confirm `partitions.csv` has two app slots and TRMNL is flashed to the OTA-counterpart slot (`esp_ota_get_next_update_partition` returns non-null).
- [ ] Heap unchanged when feature disabled (compile flag off).
- [ ] All 4 orientations unaffected (this code runs before display init).

## Atomic tasks

Each task is independently buildable and verifiable. Do them in order — later tasks depend on earlier ones — but each leaves the firmware in a working, flashable state.

### T1. Add `bootToTrmnlOnCharge` settings field
- **Change**: `crosspoint-reader/src/CrossPointSettings.h` — add `uint8_t bootToTrmnlOnCharge = 0;`. `JsonSettingsIO.cpp` — add load/save line for the new key (mirror an existing `uint8_t` toggle).
- **Deliverable**: field exists, defaults to 0, persists across reboots when written.
- **Verify**:
  - `uv run pio run` succeeds with 0 errors/warnings.
  - Add a temporary `LOG_INF("TRMNL", "boot=%u", SETTINGS.bootToTrmnlOnCharge);` in `setup()`, flash, observe `0` on first boot.
  - Manually edit settings JSON on SD to `"bootToTrmnlOnCharge": 1`, reboot, observe `1` in log. Remove the temporary log.

### T2. Add i18n string `STR_BOOT_TO_TRMNL_ON_CHARGE`
- **Change**: `crosspoint-reader/lib/I18n/translations/english.yaml` — add key. Run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`.
- **Deliverable**: new `StrId::STR_BOOT_TO_TRMNL_ON_CHARGE` enum + translation table entry. YAML + `I18nKeys.h` + `I18nStrings.h` staged for commit; `I18nStrings.cpp` not staged (pre-commit hook).
- **Verify**:
  - `grep -n "STR_BOOT_TO_TRMNL_ON_CHARGE" lib/I18n/I18nKeys.h` returns a line.
  - `tr(STR_BOOT_TO_TRMNL_ON_CHARGE)` compiles in a throwaway test call (delete after).
  - `git status` shows the YAML staged; `git diff --cached --name-only` does not include `I18nStrings.cpp`.

### T3. Surface the toggle in Settings UI
- **Change**: `crosspoint-reader/src/SettingsList.h` — inside the existing `Capability::TrmnlSwitch` block (~line 241), add a `Toggle` entry bound to `SETTINGS.bootToTrmnlOnCharge` with label `STR_BOOT_TO_TRMNL_ON_CHARGE`.
- **Deliverable**: setting visible in Settings menu when `ENABLE_TRMNL_SWITCH=1`; hidden otherwise. Toggling persists.
- **Verify**:
  - Build with `ENABLE_TRMNL_SWITCH=1`, flash, navigate Settings → toggle visible.
  - Toggle on, reboot, return to Settings → still on. Toggle off, reboot → still off.
  - Build with `ENABLE_TRMNL_SWITCH=0`, flash → toggle absent from menu.

### T4. Implement `maybeBootToTrmnl()` in trmnl_switch feature
- **Change**:
  - `crosspoint-reader/src/features/trmnl_switch/Registration.h` — declare `void maybeBootToTrmnl(bool usbConnectedAtBoot);`.
  - `Registration.cpp` — under `#if ENABLE_TRMNL_SWITCH`, implement: bail if `!usbConnectedAtBoot`, bail if `SETTINGS.bootToTrmnlOnCharge == 0`, fetch `esp_ota_get_next_update_partition(nullptr)`, bail if null, log, `esp_ota_set_boot_partition(next)`, `esp_restart()`. Empty no-op when flag is off.
- **Deliverable**: function exists and is unit-callable. Not yet wired into `setup()`.
- **Verify**:
  - Builds clean both with and without `ENABLE_TRMNL_SWITCH`.
  - Temporarily call from a debug menu action or button shortcut; confirm it switches partition only when both conditions are true. (Or defer this verify to T5.)

### T5. Wire boot decision into `setup()`
- **Change**: `crosspoint-reader/src/main.cpp` — after settings have loaded (post-`Storage.begin()` + settings read) and **before** `setupDisplayAndFonts()`, call `features::trmnl_switch::maybeBootToTrmnl(usbConnectedAtBoot)`.
- **Deliverable**: end-to-end behavior: charging + setting on → TRMNL; otherwise → reader.
- **Verify** (hardware, all four cases):
  - Setting OFF, unplugged → reader boots.
  - Setting OFF, charging → reader boots.
  - Setting ON, unplugged → reader boots.
  - Setting ON, charging → TRMNL boots; serial log shows `Switching to <partition>` line before reset.

### T6. Pre-switch splash
- **Change**: `Registration.cpp` — before `esp_ota_set_boot_partition`, render a minimal "Switching to TRMNL…" message. Reuse `BootActivity`'s pattern (logo + centered text) or a stripped-down direct `renderer.drawCenteredText` + `displayBuffer()`.
- **Constraint**: display must be initialized first. Either (a) accept that we now run *after* `setupDisplayAndFonts()` (move the call site in `main.cpp`), or (b) do a minimal `HalDisplay::begin()` + clear + draw without full font registry.
- **Deliverable**: visible "Switching to TRMNL…" frame for ~500ms before reboot.
- **Verify**: power cycle while plugged with setting on; eyeball the splash. Confirm reader-only boot path is unaffected by the new ordering.

### T7. Asymmetry guard log
- **Change**: at boot (start of `setup()` or in `maybeBootToTrmnl`), if `esp_ota_get_running_partition()` is not the expected default slot, emit `LOG_WRN("TRMNL", "running on unexpected partition; auto-switch may bounce");`.
- **Deliverable**: log line present in unexpected-partition case; silent otherwise.
- **Verify**: flash to slot 0, observe no warning. Manually set boot partition to slot 1 + reboot once, observe warning line.

### T8. Documentation
- **Change**: append a short paragraph to `crosspoint-reader/USER_GUIDE.md` (or relevant settings doc) describing the toggle, the asymmetry (TRMNL won't auto-return), and the recommendation that reader be the default boot partition.
- **Deliverable**: doc paragraph exists, references the setting label exactly.
- **Verify**: `grep -n "Boot to TRMNL" crosspoint-reader/USER_GUIDE.md` returns a hit.

### T9. Final hardware acceptance
- **Deliverable**: signed-off hardware test in all 4 orientations (the new code runs before display init in T1–T5; T6 splash needs orientation check).
- **Verify**:
  - All four T5 cases pass.
  - All four T6 splash orientations render readably.
  - `ESP.getFreeHeap()` post-boot unchanged (±1KB) vs. pre-feature baseline.
  - `uv run pio check` clean. `clang-format` clean. CI green.

## Out of scope

- TRMNL-side counterpart logic.
- Auto-return to reader on unplug.
- NVS-backed setting (Option B).
- RTC-memory bounce flag.

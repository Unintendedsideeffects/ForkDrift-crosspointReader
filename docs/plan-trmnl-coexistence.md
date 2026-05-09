# Plan: TRMNL ↔ ForkDrift coexistence (multi-week strategic plan)

> Supersedes the architectural direction in `plan-trmnl-boot-on-charge.md`.
> That earlier plan (T1–T9) shipped the **reader-side toggle** and stays valid as the user-facing entry point.
> This document covers the **system architecture** that makes the toggle actually deliver TRMNL.

## TL;DR

Add a **256 KB factory partition** holding a tiny recovery shim. Dual-boot is an **explicit user choice** at install time via the web flasher: "Install ForkDrift + TRMNL Dual-Boot" runs a full flash (coexist layout + factory shim + both apps preinstalled) and instructs the user to download a matched **SD backup package** to `/.crosspoint/dashboard/`. That package is the canonical "what should be in flash" source — recovery shim restores from it; ForkDrift's post-OTA self-heal restores TRMNL from it. ForkDrift app slot shrinks 6.25 → 6.0 MB (accepted).

**Architecture invariants:**
- **SD package = source of truth.** If a slot's contents don't match the package, it's the slot that's wrong (after a destructive event). Restoration flows always pull from SD.
- **Factory shim handles catastrophe only.** Button-combo, both slots invalid, otadata corrupt. Not the everyday OTA path.
- **ForkDrift OTA stays direct.** Standard ESP-IDF OTA writes to next slot, reboots into it. After successful boot, ForkDrift restores TRMNL to the now-stale slot from the SD package. (Asymmetric — TRMNL doesn't restore ForkDrift; that's covered by recovery shim + manual reflash documentation.)

## Why this architecture

- ForkDrift's existing partition table already has two 6.25 MB OTA slots. Reusing them as `app0=ForkDrift, app1=TRMNL` is mechanically free **except** that OTA semantics ("write to the other slot") destroy the dashboard on every reader update.
- A factory partition decouples writing from running. The running firmware is never the firmware writing itself; recovery shim writes from a clean-slate boot.
- The shim doubles as **the only recovery anchor** — boot-combo, corrupted otadata, mid-OTA power loss, "TRMNL just wiped my reader" all converge on the same code path.
- ForkDrift's only on-chip storage usage today is `otadata` (8 KB). NVS, SPIFFS, coredump are declared but unused (`grep` confirms zero `SPIFFS.begin` / `nvs_open` / `Preferences` in src+lib). All three of those regions are free real estate for TRMNL.

## Resource map (verified)

| Region | Today | Under coexistence | Conflict? |
|---|---|---|---|
| `nvs` 0x9000 / 20 KB | ForkDrift unused | TRMNL exclusive (WiFi creds, prefs) | none |
| `otadata` 0xe000 / 8 KB | ForkDrift writes | Both write (by design) | safe — that's the OTA contract |
| `app0` 0x10000 | ForkDrift 6.25 MB | ForkDrift 6.0 MB (5.49 used) | shrinks by 256 KB |
| `app1` 0x610000 | OTA scratch 6.25 MB | TRMNL 6.0 MB (~1–2 MB used) | shrinks by 256 KB |
| `factory` *(new)* 0xc10000 | n/a | Recovery shim 256 KB | new |
| `spiffs` 0xc50000 / 3.6 MB | ForkDrift unused | TRMNL exclusive (image cache) | none |
| `coredump` 0xff0000 / 64 KB | unused | shared | safe |

ForkDrift settings live on **SD card**, not NVS. Migration touches no user data.

## SD package format

Path: `/.crosspoint/dashboard/` on SD card root. Created by the flasher at dual-boot install time, downloaded by the user as a single zip and extracted.

```
/.crosspoint/dashboard/
├── manifest.json                # versions, sha256s, build dates
├── forkdrift.bin                # current ForkDrift app (~5.5 MB)
├── trmnl.bin                    # current TRMNL app (~1-2 MB)
├── partitions-coexist.bin       # binary partition table (~3 KB, diagnostic only)
└── bootloader.bin               # second-stage bootloader (~32 KB, diagnostic only)
```

`manifest.json` schema (v1):
```json
{
  "schemaVersion": 1,
  "partitionLayout": "coexist",
  "forkdrift": {
    "version": "...",
    "sha256": "<hex>",
    "filename": "forkdrift.bin",
    "size": 5497284
  },
  "trmnl": {
    "version": "...",
    "sha256": "<hex>",
    "filename": "trmnl.bin",
    "size": 1234567
  },
  "createdAt": "ISO-8601 UTC",
  "createdBy": "xteink-flasher@<version>"
}
```

**Lifecycle:**
- Created by the flasher at dual-boot install. User downloads zip, extracts to SD root.
- ForkDrift's OTA, on successful boot of new ForkDrift, rewrites `forkdrift.bin` + updates manifest with new version/sha. Keeps SD package current.
- TRMNL has no awareness of the package — when TRMNL auto-updates, `trmnl.bin` goes stale. Mitigated by a "Refresh TRMNL backup" settings action that ForkDrift exposes (reads app1's current contents, writes to SD, updates manifest). Run after each TRMNL update the user notices.
- Recovery shim and ForkDrift's post-OTA self-heal both read from this package.

## Restoration decision logic (canonical)

A "restore needed" check runs in two places: the recovery shim (after catastrophe) and ForkDrift's `setup()` (after OTA self-heal). Both follow the same logic:

1. Mount SD. If unavailable → continue with current state, log warning, no restoration possible.
2. Read `manifest.json`. If missing/invalid → log warning, no restoration possible.
3. For each app slot:
   - Call `esp_ota_get_partition_description(slot, &desc)`.
   - If invalid app OR `desc.project_name` doesn't match expected name (`"crosspoint-reader"` for ForkDrift slot, `"trmnl"` for TRMNL slot) OR `desc.version` predates the manifest's recorded version: **slot needs restoration**.
4. For each slot needing restoration:
   - Read corresponding `.bin` from SD, verify SHA256 against manifest.
   - On verify pass: ESP-IDF OTA write to that slot.
   - On verify fail: log error, leave slot alone, surface to user.
5. Restore boot pointer to whichever slot the caller intended (factory→app0 typically; ForkDrift→keeps current).

## Architecture diagrams

### Boot flow (post-coexistence)

```
power on
   │
   ▼
second-stage bootloader
   │
   ├── otadata valid + slot OK ──► boot ota_X (ForkDrift or TRMNL)
   │
   └── otadata invalid OR slot fails app validation
       │
       ▼
       boot factory (recovery shim)
       │
       ├── boot-combo held? ──► offer slot menu
       ├── /firmware-restore-app0.bin? ──► write app0, set boot, reboot
       ├── /firmware-restore-app1.bin? ──► write app1, set boot, reboot
       └── nothing ──► display "Recovery Mode — insert SD with firmware*.bin"
```

### ForkDrift OTA flow (direct + self-heal)

```
running ForkDrift (e.g. app0)
   │
   ▼
download new ForkDrift .bin
   │
   ▼
esp_ota_begin(next_partition = app1)  ← overwrites whatever is in app1 (TRMNL, if dual-boot)
   │
   ▼
write + esp_ota_set_boot_partition(app1) + esp_restart
   │
   ▼
boot new ForkDrift from app1
   │
   ▼ setup()
   │
   ▼ post-OTA self-heal:
   │   • detect we just OTA'd
   │   • read SD /.crosspoint/dashboard/manifest.json
   │   • app0 now holds OLD ForkDrift, but SD says TRMNL should be in the "other" slot
   │   • write trmnl.bin from SD to app0 (not running, safe to overwrite)
   │   • update SD manifest: forkdrift.bin <- new running version
   │
   ▼
home screen, dual-boot intact
```

### Recovery shim flow (catastrophe only)

```
boot triggers shim:
   • bootloader fallback (otadata invalid OR slot fails app validation)
   • esp_ota_set_boot_partition(factory) explicitly called
   │
   ▼
recovery shim
   │
   ├── boot-combo held? ──► slot picker UI
   │
   ├── mount SD, read /.crosspoint/dashboard/manifest.json
   ├── for each slot, run canonical restoration decision logic
   ├── write missing/stale slots from SD package (SHA256-verified)
   ├── set boot to default app (app0) or user-picked slot
   └── reboot
```

## Phase plan

Each phase ends in a **flashable, working device** so we can do stability runs (multi-day uptime, real reading + dashboard sessions) before committing to the next phase. **Don't move forward until the prior phase's stability bake is clean.**

### P0 — already done (recap)
- T1–T8 from `plan-trmnl-boot-on-charge.md`: toggle, settings, i18n, splash, USER_GUIDE entry, asymmetry-guard log.
- T9 hardware verification of the toggle itself: still pending (4 cases × 4 orientations).
- Build green at 44.4% RAM / 83.9% flash.

**Carry forward into stability bake**: the toggle is harmless when `app1` has no valid app, so it can stay enabled in the field even before the factory work lands.

### P1 — Partition table change (1 day work, then 3-day bake)
- New `partitions-coexist.csv` with the layout above.
- ForkDrift builds against it. Verify `app0` size headroom is acceptable (target ≥ 0.4 MB free).
- Document the migration cost: existing users will eventually need a one-time bootloader + partition-table reflash.
- Ship as a **build-flag-gated** alternative table at first: default builds keep the old layout, opt-in `-DPARTITION_LAYOUT_COEXIST` switches in. Lets us bake without disturbing anyone.

**Stability gate:** flash a coexist-layout build to a dev device, run normal reading workload for 3 days. No new crashes, no SD corruption, heap profile within ±1 KB of baseline. Compare `ESP.getFreeHeap()` measurements against the prior layout.

**Verifiable deliverable:** `partitions-coexist.csv` committed; coexist-layout firmware boots, reads EPUBs, sleeps/wakes, OTA still works (writing to the other slot — dual-boot not yet active).

### P2 — Recovery shim skeleton (2 days work, then 3-day bake)
- New PIO env / project root: `crosspoint-reader/recovery/`. Stripped Arduino app, no fonts, no I18n, no UITheme, no FreeRTOS bells.
- Reuses `HalStorage` and `HalDisplay` only. Direct framebuffer text.
- Implements:
  - Mount SD, read `/.crosspoint/dashboard/manifest.json`.
  - For each app slot, run the **canonical restoration decision logic** (see "Restoration decision logic" section above).
  - SHA256 verify (`mbedtls_md` in ESP-IDF, no extra deps).
  - ESP-IDF OTA API write to target partition.
  - Boot-combo (Back + Confirm) detection during first 1s of shim entry → present a tiny "Pick slot to boot" menu (just two options); skip restoration logic.
  - Status text: "Recovery Mode\nRestoring app0…\n42%\nDo not power off".
  - On finish: set boot to whichever slot completes successfully (default app0), reboot.
- Size budget: < 200 KB compiled. Hard fail at 240 KB.
- Build artifact: `factory.bin`, flashable to factory partition via esptool.

**Stability gate:** Flash factory + ForkDrift on a coexist-layout device with a populated SD package. Manually trigger every recovery path:
1. Stamp `esp_ota_set_boot_partition(factory)` from a debug build → boot to factory → confirm app0 is intact and reboots back to it.
2. Erase app1 → boot factory → confirm it restores TRMNL from SD package.
3. Erase app0 → boot factory → confirm it restores ForkDrift from SD package.
4. Corrupt otadata (`esp_ota_set_boot_partition(invalid)` is hard; instead manually overwrite otadata bytes via debug serial) → confirm bootloader falls back to factory.
5. Hold boot-combo from cold start → menu appears, both choices work.
6. Power-pull during shim write → next boot enters factory and retries cleanly (manifest still valid, partial slot fails app validation, retry).
7. SD card removed before factory boots → factory shows "SD missing — insert and reset" and waits.

**Verifiable deliverable:** factory.bin under 240 KB; all seven scenarios pass; no other ForkDrift behavior changes.

### P3 — ForkDrift post-OTA self-heal (2 days work, then 5-day bake)
- ForkDrift OTA stays direct (writes to `next_update_partition`, sets boot, reboots — same as today).
- After successful boot of new ForkDrift, in `setup()` after `Storage.begin()`:
  - Determine if we just OTA'd (compare running partition vs. settings-recorded "expected boot slot" from prior boot).
  - If yes and dual-boot is active (manifest.json present in SD package):
    - Run the canonical restoration decision logic, scoped to the *non-running* slot.
    - If that slot is missing TRMNL (or vice versa), restore from SD package.
    - Update the SD package: write the new ForkDrift `.bin` and refresh manifest with new version/sha. (This keeps the package current for *future* restorations.)
- Add a "Refresh TRMNL backup" settings action (manual): reads app1's current contents, writes to `/.crosspoint/dashboard/trmnl.bin`, updates manifest. Used after the user notices TRMNL has auto-updated.

**Stability gate:**
1. Install dual-boot, then perform 5 consecutive ForkDrift OTAs. After each: TRMNL still in `app1`, SD package's `forkdrift.bin` matches new version.
2. Three SD-firmware updates from a fresh `firmware*.bin`. TRMNL preserved.
3. Mid-restoration power-pull → next boot detects TRMNL slot still invalid → re-runs restoration → succeeds.
4. Long-run: 7 days of normal dual-boot use including at least one ForkDrift OTA.

**Verifiable deliverable:** dual-boot survives ForkDrift OTAs and SD updates. SD package stays current.

### P4 — Move boot-combo into shim, polish recovery UX (1 day work, 2-day bake)
- Remove any `setup()`-time boot-combo detection from ForkDrift (originally Step D in the prior plan).
- Recovery shim's boot-combo handler is the canonical entry.
- Document the combo in USER_GUIDE.
- Optional: a "Boot Slot Picker" UI in the shim (cosmetic, just two buttons + status).

**Stability gate:** boot-combo invokes recovery from running ForkDrift, running TRMNL, and a "no valid app" state. All three reach the slot picker.

**Verifiable deliverable:** single source of truth for recovery; ForkDrift `setup()` is leaner; USER_GUIDE updated.

### P5 — Optional in-ForkDrift TRMNL install (deferred / nice-to-have)
*Originally a hard requirement; demoted because the flasher's "Dual Boot" install path covers the primary install case.*

- Add an "Install TRMNL Dashboard" settings action for **single-boot ForkDrift users who later decide to add TRMNL** without going back to the web flasher.
  - Reads `/.crosspoint/dashboard/trmnl.bin` from SD (user pre-loaded the SD package zip).
  - Validates SHA256 against manifest.
  - Writes to `app1` directly via ESP-IDF OTA API (allowed — running from app0).
  - Updates settings flag.
- Add "Remove TRMNL Dashboard" — erases app1, clears flag.

**Stability gate:** install/remove cycle 3 times, each time toggle works as expected after.

**Verifiable deliverable:** post-install dual-boot path that doesn't require web-serial. **Skip this phase entirely if the flasher's install path is sufficient.**

### P6 — Flasher: "Dual Boot" install + multi-layout fast flash (3 days work, 2-day bake)
The biggest and most user-facing single phase. Two related changes in `xteink-flasher`:

**(a) Multi-layout partition validation:**
- Replace `expectedPartitionTable` constant (currently `useEspOperations.ts:19-26`, single hardcoded table) with a list of known-good layouts: `[LEGACY_TABLE, COEXIST_TABLE]`.
- Read partition table from device, match against the list, store the matched layout.
- All fast-flash paths use the matched layout's offsets/sizes for app0/app1 writes.
- Refuse to operate on unknown layouts (current behavior).

**(b) "Install ForkDrift + TRMNL Dual-Boot" flow:**
- New top-level button in the UI alongside existing options.
- Performs a **full flash** of the coexist layout:
  - bootloader.bin at 0x0
  - partitions-coexist.bin at 0x8000
  - otadata zeroed at 0xe000
  - app0 = ForkDrift latest
  - app1 = TRMNL coexist build
  - factory = recovery shim
- After flash: prompt user to download the matched SD package zip (server-generated, contains all four `.bin` files + manifest).
- User extracts to SD root, inserts SD, powers on device. Done.

**(c) "Add TRMNL to existing ForkDrift" (optional):**
- Detect coexist layout. Write only app1 from a fresh TRMNL build.
- Generate fresh manifest entry, prompt user to update SD package.

**Stability gate:** install three test devices via the new flow. Each:
- Boots ForkDrift on first power-up.
- Toggle TRMNL → boots TRMNL.
- Power-cycle without charging → boots ForkDrift.
- Boot-combo → recovery shim.
- One ForkDrift OTA → both apps still present, SD package updated.

**Verifiable deliverable:** end-to-end install via web flasher works; legacy users still flashable with legacy fast-flash.

### P7 — TRMNL fork build (half day work, 5-day bake)
- Fork `usetrmnl/trmnl-firmware`. Add:
  ```ini
  [env:xteink_x4_coexist]
  extends = env:xteink_x4
  board_build.partitions = forkdrift_coexist_partitions.csv
  build_flags = ${env:xteink_x4.build_flags} -DBOARD_XTEINK_X4_COEXIST
  ```
- Drop in a verbatim `forkdrift_coexist_partitions.csv` matching ForkDrift's `partitions-coexist.csv`.
- Build artifact `xteink_x4_coexist.bin` published as a GitHub release on the fork (consumed by the flasher and by the SD package).
- Open upstream PR offering it (low expectation of merge; useful as a public reference).

**Stability gate:** TRMNL `xteink_x4_coexist.bin` installed via P6 dual-boot flow runs for 5 days hitting real TRMNL servers, fetching dashboards on schedule. Test boot-combo recovery from inside TRMNL succeeds.

**Verifiable deliverable:** working TRMNL binary that lives at offset 0x610000 against ForkDrift's partition table; published; pinned to a known commit.

## Decisions deferred / pending

| Decision | Default until decided |
|---|---|
| Whether to make coexist layout the **default** ForkDrift build, vs. opt-in flag | Opt-in flag during P1–P5; flip to default at P6 |
| Whether to support TRMNL auto-update wiping ForkDrift, or document "disable TRMNL auto-updates" | Document the workaround; long-term, request a TRMNL-side cooperation PR |
| Whether to host the migration installer on Vercel/Pages or rely on `trmnl.com/flash` | Self-host on GitHub Pages until P6 lands |
| Boot-combo button choice (Back+Confirm vs power+side) | Back+Confirm; revisit if conflicts with sleep/wake |

## Risk register

| Risk | Mitigation |
|---|---|
| Recovery shim grows past 256 KB | Hard size assert in CI; strip aggressively (no Arduino String, no fonts, no logging at WARN+) |
| Mid-OTA power loss during shim write | Marker file persists; next boot retries from factory. SHA256 means partial writes never validate. |
| TRMNL OTA wipes ForkDrift in field | Boot-combo + factory + ForkDrift backup on SD = guaranteed manual recovery without host computer. Document loudly. |
| Existing users skip the migration | P1's opt-in flag means non-migrated users see no change. P6's installer is the upgrade trigger; tie it to a major version bump. |
| Heap regression from new factory partition | P1 stability gate measures heap; if regressed, abort and investigate. |
| SD card slow / corrupt during recovery | Recovery shim displays "SD error" and waits; user can swap card and reboot. Never bricks. |

## Out of scope

- TRMNL auto-update cooperation (would require upstream PR to TRMNL).
- A "boot menu" UX in factory beyond the binary slot picker (deferred until users ask for it).
- Backing up ForkDrift settings into NVS or factory partition (SD is the source of truth).
- Cross-firmware shared state (settings sync between TRMNL and ForkDrift) — not needed for the dual-boot use case.
- A full bootloader replacement. We're using stock ESP-IDF bootloader + factory partition semantics; no custom boot logic.

## Stability run protocol (apply to every phase)

1. **Build the phase artifact and flash to a dedicated bake device.** Don't run bakes on the daily-driver.
2. **Baseline metrics before:** `ESP.getFreeHeap()` after boot, after home screen, after 1 hour of reading. Coredump partition empty.
3. **Workload:** at least one of every supported activity per day (read EPUB, browse files, settings change, sleep/wake, OTA if applicable).
4. **Pass criteria:** zero unexpected reboots, heap within ±2 KB of baseline at matching lifecycle points, no coredumps, no SD errors in `crosspoint-debug.log`.
5. **Fail criteria:** any unexpected reboot, heap regression > 5 KB sustained, coredump captured, persistent SD errors. **Halt the phase, root-cause, then either fix-and-re-bake or revert.**
6. **Document each bake:** start date, build SHA, end date, observations. Keep notes in `bakes/PNN-bake-YYYYMMDD.md`.

## Estimated total timeline (revised)

- Engineering: ~10 working days across P1–P7 (P5 demoted, P3 simplified).
- Stability bakes: 3+3+5+2+(3 if P5)+2+5 = 20–23 calendar days, parallelizable with the next phase's engineering.
- **Realistic calendar: ~4–5 weeks** if engineering and bakes overlap. ~6 weeks if strictly sequential.

## What ships if we never finish

After P0 + P1 alone: opt-in coexist layout that nobody uses but doesn't hurt anyone. The toggle from T1–T8 stays harmless. Zero regression.

After P0 + P1 + P2: device has a recovery shim. ForkDrift works as before. New stability anchor; useful even without TRMNL.

After P3: ForkDrift OTA goes through factory. More robust than today's OTA. Worth shipping on its own.

After P5 + P7: full dual-boot delivered. Everything below this line is polish.

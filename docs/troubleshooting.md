---
title: Troubleshooting
nav_order: 14
---

# Troubleshooting

This document shows common issues and possible solutions while using the device features.

- [Troubleshooting](#troubleshooting)
  - [Cannot See the Device on the Network](#cannot-see-the-device-on-the-network)
  - [Connection Drops or Times Out](#connection-drops-or-times-out)
  - [Upload Fails](#upload-fails)
  - [Saved Password Not Working](#saved-password-not-working)

### Cannot See the Device on the Network

**Problem:** Browser shows "Cannot connect" or "Site can't be reached"

**Solutions:**

1. Verify both devices are on the correct network (must be a 2.4ghz network)
   - Check your computer/phone Wi-Fi settings
   - In **Join Network** mode, your computer/phone and CrossInk must be on the same Wi-Fi network
   - In **Create Hotspot** mode, your computer/phone must be connected to the `CrossPoint-Reader` hotspot
2. Double-check the IP address
   - Make sure you typed it correctly
   - Include `http://` at the beginning
   - Try the displayed IP address if `http://crosspoint.local/` does not resolve
3. Try disabling VPN if you're using one
4. Some networks have "client isolation" enabled - use Create Hotspot mode or check with your network administrator

### Connection Drops or Times Out

**Problem:** Wi-Fi connection is unstable

**Solutions:**

1. Move closer to the Wi-Fi router, or use Create Hotspot mode for a direct connection
2. Check signal strength on the device (should be at least `||` or better)
3. Avoid interference from other devices
4. Try a different Wi-Fi network if available (must be a 2.4ghz network)

### Upload Fails

**Problem:** File upload doesn't complete or shows an error

**Solutions:**

1. Check that the SD card has enough free space
2. Check that the filename is valid for the SD card filesystem
3. Try uploading a smaller file first to test
4. Refresh the browser page and try again
5. If WebSocket upload fails repeatedly, refresh the page and retry with the HTTP fallback path

### Saved Password Not Working

**Problem:** Device fails to connect with saved credentials. This often happens when swapping between firmwares or devices due to the password hashing mechanism.

**Solutions:**

1. When connection fails, you'll be prompted to "Forget Network"
2. Select **Yes** to remove the saved password
3. Reconnect and enter the password again
4. Choose to save the new password

### Device Information

Open **Settings → System → About**. Use Up/Down or Left/Right to scroll with
buttons, or swipe up/down on touch devices. The header Back arrow and physical
Back return to the same Settings selection. Diagnostic rows are read-only.
Confirm, or the **Export support info** touch button, opens the export scope
selector. Nothing is written until you choose a scope and confirm it.

The device profile and display controller reflect the SDK's active profile
including boot-time panel detection. Resolution is the physical panel size,
independent of screen rotation. The build row shows the firmware device target,
source revision and whether tracked sources were modified at build time. Flash
capacity is the chip's capacity, not free firmware update space. The SDK row is
the running ESP-IDF version.

Touch and external RTC/IMU distinguish hardware not present from configured
hardware unavailable after initialization. Frontlight presence describes the
board's supported hardware; it is not a test of LED operation. SD capacity is
reported only for mounted storage; computing FAT free space is deliberately
avoided. No network connection or sensor wake-up is required.

Uptime, internal RAM free/largest block and PSRAM free/largest block are
snapshots taken when About opens, including About's own small allocation.
Internal RAM minimum is the allocator's low-water figure since boot. Internal
RAM uses the 8-bit internal allocation pool and excludes PSRAM. A large PSRAM
free value does not prove an internal-RAM allocation can succeed. PSRAM absent
on C3 is shown as **Not present**. Reopen About to capture another snapshot.
Reset reason is the numeric ESP-IDF `esp_reset_reason_t` code, useful alongside
the SDK version (1: power-on, 3: software restart, 4: panic, 5/6/7: watchdog,
8: deep-sleep wake, 9: brownout). It is a boot reason, not a captured crash log.

Simulators show the simulated profile, panel dimensions and simulated
peripherals. Chip, flash, display-controller, allocator and reset measurements
are **Unsupported** rather than fabricated hardware readings. Storage transport
and capacity are marked **Simulated** because the backend is the host filesystem.
About omits MAC addresses, chip IDs, custom device names, network addresses,
credentials, server URLs, and book information.

### Sharing Settings for Support

`/.crosspoint/crossink-settings.json` stores global preferences, including the
reader defaults, controls, status bars, frontlight schedule, language/keyboard,
library options and selected font/dictionary settings. It does not include
hardware diagnostics or per-book reader overrides. Custom device names, font
names and transfer folder paths may reveal private information: review and
remove them before sharing. Older or manually edited files may retain extra
keys that current firmware no longer writes.

Do not share the whole `.crosspoint` directory. Wi-Fi (`wifi.json`), OPDS
(`opds.json`) and KOReader (`koreader.json`) files contain network/server names,
usernames and reversibly obfuscated passwords. Obfuscation is not encryption.
Session/recent-book, bookmark, clipping and reading-stat files reveal private
paths or reading history. Use the allowlisted support export below instead of
sharing these stores wholesale.


### Export Support Information

Open **Settings → System → About → Export support info**. Choose **Device and
global settings**, or explicitly choose **Also include last opened EPUB
settings** when an EPUB is available in the current session state. The second
scope refers to that one book only; it does not browse the library or scan
history. The confirmation explains the exclusions and replacement filename.
Cancel is selected by default in both steps. Confirm writes
`/crossink-support.json` in the SD card root. There is no upload, automatic
sharing, network connection, or web-download endpoint.

The JSON has `schema: "crossink-support"` and `version: 1`. Coverage is deliberately
allowlisted, not a claim to include every persisted setting:

- **Firmware:** canonical project version (without branch suffix), source revision,
  firmware device target, tracked-source modified flag and the actual compiled
  FreeInk SDK checkout revision. `about.espIdfVersion` is the runtime ESP-IDF
  version, which is different from the FreeInk SDK revision.
- **About:** the same safe hardware fields as the screen, captured again immediately
  before the export write. Heap measurements include the export allocation.
  Values use bytes, MHz, physical pixels and seconds. Simulator runtime fields
  are marked simulated/unsupported, and allocator/chip measurements are omitted.
  PSRAM absence and initialization failure remain distinct.
- **Global preferences:** the explicitly listed numeric settings, status-bar slot
  choices/options and Quick Action IDs. Values are a mutex-protected snapshot of
  loaded in-memory global preferences, not a reread or validation of the settings
  file on disk. Numeric enums/units follow this firmware's settings schema;
  hardware-specific preferences may be retained even on unsupported hardware.
  Font/dictionary names are reduced to custom-font-selected booleans. `statusSlots`
  contains top-reader 7, bottom-reader 7, then display 3 choices; `statusOptions`
  contains percentage format, progress bar mode and thickness for top then bottom,
  followed by the top, bottom and display battery styles.
  The optional `readerOverrideMask` uses the reader-settings v11 bit order, with
  bit 18 identifying image grayscale and bit 19 the redacted SD-font-family override.
- **Configuration status:** only presence of the Wi-Fi, OPDS, KOReader and TTF
  rendering-profile JSON files. Presence does not prove valid configuration,
  credentials, a network connection, or loaded profiles. These files are not read,
  and server counts/options, custom font profiles and hardware NVS overrides are
  not collected by version 1.
- **Optional last opened EPUB context:** the current cache's reader-settings
  record and statistics-off marker only. Numeric reader fields report effective
  values (including the reader's clamping/fallback rules) and `global_default`,
  `book_override`, `global_fallback`, or `safe_mode` sources. Built-in legacy font-size steps are
  converted to physical points. Legacy custom-font steps require a font registry
  lookup; version 1 exports a null size with an explicit unavailable status instead
  of scanning fonts or labelling the stored step an effective point size. Font
  selection, dictionary, render-mode, safe-mode and auto-page-turn overrides are
  reported without custom names. A zero dictionary point size follows the
  effective reader font size. `bookStatsEnabled` is the per-book marker;
  `effectiveStatsEnabled` also respects the global tracking switch. Missing
  records inside an existing current cache inherit defaults. Invalid records,
  absent current caches (including unmigrated legacy caches), missing files and
  unsupported formats report unavailable/invalid context instead of invented
  effective settings. No cache migration or book metadata parsing occurs.

Credentials (including obfuscated values), usernames, URLs, SSIDs, MAC/chip/device
identifiers, device names, custom font names, folder/book paths, book titles,
authors, cache hashes, bookmarks, clippings, progress/history and raw logs are
excluded. The file still reveals device capabilities and personal preference
choices; review it before sharing. Excluded, unavailable and simulated information
is called out rather than represented by plausible hardware values.

The export streams through a fixed 256-byte buffer, with no complete JSON document
in memory. It writes `.tmp`, checks writes/sync/close, and retains the previous
export as `.bak` until installation succeeds. FAT does not provide a single
atomic replace operation: an interrupted rename can temporarily leave `.bak`
instead of the main filename. The next export restores that backup before
starting. Failed writes preserve the previous complete export. If rollback
fails, the UI reports that the prior export remains in `.bak`; retrieve that
file or retry. If backup cleanup fails after successful installation, the UI
reports **Saved; previous export kept as .bak**. Do not remove a recovery backup
before checking it. Storage hardware failures can still prevent recovery.

On X3/X4/Classic, test the button route, both cancellation steps and return to
Settings. On Sticky/Pro, test the export touch button, scope choices, confirmation
and header Back. On each, check exports with and without an EPUB override, then
an absent/read-only/full SD card; the prior file must survive a failed write.
Compare C3 PSRAM absence and S3 internal/PSRAM pools against serial diagnostics.
No book-cache reset or hardware flash is required to use this feature.

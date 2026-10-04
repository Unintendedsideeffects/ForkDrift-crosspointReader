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
Back return to the same Settings selection. Rows are read-only; Confirm has no
side effect. About does not write a file or change settings.

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
paths or reading history. A screenshot of About and a sanitized settings file
are useful together; CrossInk currently has no dedicated sanitized support-dump
export.

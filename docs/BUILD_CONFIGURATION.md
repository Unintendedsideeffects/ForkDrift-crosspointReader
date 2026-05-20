# Build Configuration Guide

CrossPoint Reader supports customizable firmware builds, allowing you to include only the features you need and save precious flash memory space on your ESP32-C3 device.

## Table of Contents

- [Quick Start](#quick-start)
- [Feature Reference](#feature-reference)
- [Build Profiles](#build-profiles)
- [Local Build Instructions](#local-build-instructions)
- [GitHub Actions Builds](#github-actions-builds)
- [Flash Memory Considerations](#flash-memory-considerations)
- [Troubleshooting](#troubleshooting)

## Quick Start

### Using the Feature Picker (Easiest)

1. Visit [Feature Picker](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/)
2. Select your desired features or choose a profile
3. Click "Build on GitHub Actions"
4. Wait ~5-10 minutes for the build to complete
5. Download the firmware artifact and flash to your device

### Using Command Line (Local Builds)

```bash
# Generate configuration for standard profile
uv run python scripts/generate_build_config.py --profile standard

# Build custom firmware
uv run pio run -e custom

# Flash to device
uv run pio run -e custom --target upload
```

## Feature Reference

### Bookerly Fonts

**Flag:** `ENABLE_BOOKERLY_FONTS`  
**Size Impact:** ~803KB  
**Default:** Enabled in `standard` and `full`

Adds the larger Bookerly reading sizes.

**What's included when enabled:**
- Bookerly 12pt, 16pt, 18pt (Regular, Bold, Italic, Bold-Italic)

**What's always included:**
- Noto Serif reading fonts
- Ubuntu 10pt, 12pt (UI fonts)

**When disabled:**
- Bookerly does not appear as a selectable reading family
- The remaining built-in font families continue to work

**Use case:** Disable if you do not use Bookerly and want to save about 800KB.

---

### Noto Sans Fonts

**Flag:** `ENABLE_NOTOSANS_FONTS`  
**Size Impact:** ~1009KB  
**Default:** Enabled in `standard` and `full`

Adds the larger Noto Sans reading sizes.

**What's included when enabled:**
- Noto Sans 12pt, 14pt, 16pt, 18pt (Regular, Bold, Italic, Bold-Italic)

**What's always included:**
- Noto Serif reading fonts
- Ubuntu 10pt, 12pt (UI fonts)

**When disabled:**
- Noto Sans does not appear as a selectable reading family
- The remaining built-in font families continue to work

**Use case:** Disable if you prefer the serif defaults and want to save about 1MB.

---

### OpenDyslexic Font Pack

**Flag:** `ENABLE_OPENDYSLEXIC_FONTS`
**Size Impact:** ~2.6MB
**Default:** Disabled
**Depends on:** compile guard accepts `ENABLE_BOOKERLY_FONTS` or `ENABLE_NOTOSANS_FONTS`; the current generator metadata for generated custom profiles treats both parent packs as required

Adds OpenDyslexic 8pt, 10pt, 12pt, and 14pt fonts.

**Use case:** Enable if you want the OpenDyslexic reading family.

---

### PNG/JPEG Sleep Images

**Flag:** `ENABLE_IMAGE_SLEEP`
**Size Impact:** ~0KB in current measurements
**Default:** Enabled

Enables PNG and JPEG format support for custom sleep screen images.

**What's included when enabled:**
- PNG decoder (using PNGdec library)
- JPEG decoder (using picojpeg library)
- Support for `.png`, `.jpg`, `.jpeg` sleep images

**What's always included:**
- BMP image support (native to display library)
- Cover-based sleep screens (from EPUB covers)

**When disabled:**
- Sleep images folder only accepts `.bmp` files
- PNG/JPEG images in the sleep folder are ignored
- Cover sleep screens still work (covers are converted to display format)

**Use case:** Disable if you only use BMP sleep images or cover-based sleep screens.

---

### Book Images

**Flag:** `ENABLE_BOOK_IMAGES`
**Size Impact:** ~0KB
**Default:** Enabled

Controls inline image rendering inside EPUB and Markdown books.

**When enabled:**
- `<img>` content in EPUB chapters is rendered inline
- Markdown image syntax (`![alt](path)`) renders inline images when supported

**When disabled:**
- Inline images are replaced with fallback text labels
- Text layout, navigation, and chapter rendering remain available

**Use case:** Disable for text-only reading behavior or to troubleshoot problematic inline image content.

---

### Markdown/Obsidian

**Flag:** `ENABLE_MARKDOWN`
**Size Impact:** ~176KB
**Default:** Disabled in `standard`, enabled in `full`

Full Markdown rendering with Obsidian vault compatibility.

**What's included when enabled:**
- Markdown parser (md4c library)
- HTML renderer for Markdown
- Obsidian-specific features:
  - Wikilinks (`[[Page]]`)
  - Callouts (note, warning, tip, etc.)
  - Embedded notes
  - Metadata frontmatter
- Markdown extensions:
  - Tables
  - Task lists
  - Strikethrough
  - Footnotes

**When disabled:**
- `.md` files show "Markdown support not available" message
- Todo feature defaults to `.txt` format instead of `.md`
- Obsidian vaults cannot be opened

**Use case:** Disable if you only read EPUB/TXT files and never use Markdown notes.

---

### KOReader Sync

**Flag:** `ENABLE_KOREADER_SYNC`
**Size Impact:** ~2KB
**Default:** Disabled
**Depends on:** `ENABLE_INTEGRATIONS`

Syncs reading progress with KOReader-compatible metadata.

**When disabled:**
- KOReader sync actions are unavailable
- Core EPUB/TXT/Markdown reading is unaffected

**Use case:** Enable only if you actively use KOReader sync flows.

---

### Calibre Sync

**Flag:** `ENABLE_CALIBRE_SYNC`
**Size Impact:** ~17KB
**Default:** Disabled
**Depends on:** `ENABLE_INTEGRATIONS`

Syncs metadata and reading progress with Calibre.

**When disabled:**
- Calibre sync actions are unavailable
- Core reader behavior is unchanged

**Use case:** Enable only if you use Calibre integration.

---

### Integrations Base

**Flag:** `ENABLE_INTEGRATIONS`
**Size Impact:** ~0KB
**Default:** Disabled

Shared runtime hooks required by remote sync integrations.

**When disabled:**
- KOReader and Calibre sync features are forced off
- Core reading behavior is unchanged

**Use case:** Enable only when you need KOReader sync and/or Calibre OPDS flows.

---

### Background Web Server

**Flag:** `ENABLE_BACKGROUND_SERVER`
**Size Impact:** ~4KB
**Default:** Enabled

Keeps the WiFi file management server running in the background while reading.

**What's included when enabled:**
- Background web server continues running during reading
- File uploads possible while reading a book
- WiFi stays connected in reading mode (if USB plugged in)

**Sub-features:**

- **On Charge (`ENABLE_BACKGROUND_SERVER_ON_CHARGE`):** Automatically starts the background server whenever USB power is connected, even if it was manually stopped, and keeps it running until USB power is removed or connectivity fails.
- **Always On (`ENABLE_BACKGROUND_SERVER_ALWAYS`):** Auto-connects to WiFi on wake even when not charging. This ensures the server is always reachable but increases battery drain during active use.

**When disabled:**
- Web server only runs in Home/Library views
- Reading automatically stops the web server
- Minimal power/memory impact

**Use case:** Disable for slightly lower memory usage if you never upload files while reading. Enable "Always On" if you want seamless remote access without needing to plug in.

---

### Remote Keyboard Input

**Flag:** `ENABLE_REMOTE_KEYBOARD_INPUT`
**Size Impact:** ~12KB
**Default:** Enabled

Adds a modular remote text-entry path for any on-device keyboard prompt:

- Prefers the Android companion app when it is already connected over USB serial or WiFi
- Falls back to a browser page at `/remote-input` with a QR code shown on-device
- Starts a temporary hotspot automatically when WiFi is unavailable so the browser fallback remains reachable
- Exposes the `remote_keyboard_input` capability in `/api/plugins`, `/api/features`, and the USB `plugins` command

**When disabled:**
- Keyboard entry always stays on-device
- `/remote-input` and `/api/remote-keyboard/*` routes are not registered
- Android and browser clients will not discover remote keyboard support

**Use case:** Keep enabled if you want faster password/search/text entry from the Android app or a nearby browser.

---

### WiFi Clock

**Flag:** `ENABLE_WIFI_CLOCK`
**Size Impact:** ~2KB
**Default:** Enabled in `standard` and `full`

Adds the NTP-backed Roman numeral clock label used by the home header and status overlays.

**When enabled:**
- Syncs time over WiFi with NTP
- Shows a quarter-hour Roman clock label when system time is valid

**When disabled:**
- Roman clock labels are hidden from the UI
- Downstream clock-based optional features are unavailable

**Use case:** Keep enabled if you want any on-device WiFi-synced clock display.

---

### Roman Clock Sleep Screen

**Flag:** `ENABLE_ROMAN_CLOCK_SLEEP`
**Size Impact:** ~3KB
**Default:** Disabled
**Depends on:** `ENABLE_WIFI_CLOCK`

Adds a dedicated sleep-screen mode that renders the existing WiFi clock as large block Roman numerals.

**When enabled:**
- The Settings screen gains a `Roman Clock` sleep-screen option
- Sleep mode can show the current quarter-hour Roman clock on a clean full-screen layout

**When disabled:**
- Existing sleep-screen options remain unchanged
- WiFi Clock still works in the header/status bar if enabled

**Use case:** Enable when you want the sleep screen itself to act as a minimalist Roman numeral clock.

---

### Home Media Picker

**Flag:** `ENABLE_HOME_MEDIA_PICKER`
**Size Impact:** ~0KB
**Default:** Enabled

Replaces the classic Home list selector with a streamlined media-style layout:

- Left/Right controls the horizontal recent-book shelf
- Up/Down controls the vertical action menu
- Confirm opens the highlighted menu action

**When disabled:**
- Home screen uses the legacy "Continue Reading + vertical menu" layout
- Navigation falls back to single-axis menu selection

**Use case:** Disable if you prefer the legacy Home behavior.

---

### Pokemon Wallpaper Plugin

**Flag:** `ENABLE_POKEMON_WALLPAPER_PLUGIN`
**Size Impact:** ~34KB
**Default:** Disabled

Adds an optional browser-side Pokemon wallpaper generator page at `/plugins/pokemon-wallpaper`:

- Runs in the browser, not on the device CPU
- Generates grayscale X4 wallpapers from PokeAPI data
- Uploads directly to `/sleep/pokedex` using the existing web upload API
- Can optionally use a baked local cache for offline / low-latency firmware previews

Optional baked cache workflow:

1. Export `pokemon_cache.json` from the companion `pokedex.html` tool.
2. Run `python scripts/inject_pokemon_cache.py /path/to/pokemon_cache.json`
3. Build normally with `uv run pio run`

The cache is stored in a local sidecar file and injected during `scripts/build_html.py`;
the source `PokemonWallpaperPluginPage.html` stays unchanged.

**When disabled:**
- `/plugins/pokemon-wallpaper` route is not registered
- Files page does not show the Pokedex plugin launcher button

**Use case:** Enable when you want integrated Pokemon wallpaper creation in the device web UI.

---

### Pokemon Party

**Flag:** `ENABLE_POKEMON_PARTY`
**Size Impact:** ~4KB
**Default:** Disabled
**Depends on:** None

Builds on the recent-books cache to create a lightweight
Pokemon companion layer:

- Adds per-book `pokemon.json` sidecars in the existing book cache
- Exposes `GET`/`PUT`/`DELETE /api/book-pokemon`
- Extends `/api/recent` with saved Pokemon metadata plus real cached progress
- Turns recent books into a six-slot party surface in the Fork Drift home flow
- Bakes per-book cover+sprite visuals into `/sleep/pokedex/party` for both the recent-party slots and the sleep screen when a Pokemon is assigned from the web UI

**When disabled:**
- `/api/book-pokemon` routes are not registered
- The device Settings `Pokedex` shortcut is hidden
- `/plugins/pokemon-wallpaper` remains just the wallpaper/plugin surface when the wallpaper plugin is enabled

**Use case:** Enable when you want recent books to behave like a Pokemon party,
with reading progress driving level and evolution state.

---

## Build Profiles

### Lean Profile

**Size:** ~1.7MB

```bash
uv run python scripts/generate_build_config.py --profile lean
```

**Profile behavior:** Starts from a minimal `custom` environment with every optional feature flag disabled.

**Best for:**
- A smallest-possible baseline
- Custom builds where you want to add features back explicitly
- Maximum flash headroom

---

### Standard Profile (Recommended)

**Size:** ~5.0MB

```bash
uv run python scripts/generate_build_config.py --profile standard
```

**Features:**
- ✓ Bookerly Fonts
- ✓ Noto Sans Fonts
- ✓ PNG/JPEG Sleep
- ✗ Markdown/Obsidian
- ✗ Integrations Base
- ✗ KOReader Sync
- ✗ Calibre Sync
- ✓ Background Server
- ✓ Background Server On Charge
- ✗ Background Server Always
- ✓ Web WiFi Setup
- ✓ Remote Keyboard Input
- ✓ Home Media Picker
- ✓ BLE WiFi Provisioning
- ✓ User Fonts
- ✓ USB Mass Storage
- ✓ Dark Mode
- ✗ Pokemon Wallpaper Plugin
- ✗ Pokemon Party

**Best for:**
- Most users
- Good balance of features and flash space
- Includes essential reading features plus background file access

---

### Full Profile

**Size:** ~5.9MB (feature-rich build, still within the 6MB app slot)

```bash
uv run python scripts/generate_build_config.py --profile full
```

**Features:**
- ✓ Bookerly Fonts
- ✓ Noto Sans Fonts
- ✗ OpenDyslexic Font Pack
- ✓ PNG/JPEG Sleep
- ✓ Markdown/Obsidian
- ✓ Integrations Base
- ✓ KOReader Sync
- ✓ Calibre Sync
- ✓ Todo Planner
- ✓ Anki Support
- ✓ Background Server
- ✓ Background Server On Charge
- ✓ Background Server Always
- ✓ Remote Keyboard Input
- ✓ Home Media Picker
- ✓ Visual Covers
- ✓ User Fonts
- ✓ Web WiFi Setup
- ✓ USB Mass Storage
- ✓ Dark Mode
- ✗ BLE WiFi Provisioning
- ✓ Pokemon Wallpaper Plugin
- ✓ Pokemon Party

**Best for:**
- Users who want most built-in features
- Devices with adequate flash space remaining
- Power users who use Markdown/Obsidian

---

## Local Build Instructions

### Prerequisites

- `uv` plus the pinned Python/PlatformIO toolchain (`uv sync --frozen`)
- VS Code + PlatformIO IDE is optional for editor integration
- Python 3.11+
- USB-C cable for flashing

### Generate Configuration

The `generate_build_config.py` script creates a `platformio-custom.ini` file with your selected features.

**Using profiles:**
```bash
# Lean build
uv run python scripts/generate_build_config.py --profile lean

# Standard build
uv run python scripts/generate_build_config.py --profile standard

# Full build
uv run python scripts/generate_build_config.py --profile full
```

**Custom feature selection:**
```bash
# Start from lean, add specific features
uv run python scripts/generate_build_config.py --profile lean --enable bookerly_fonts --enable notosans_fonts

# Start from full, remove specific features
uv run python scripts/generate_build_config.py --profile full --disable markdown

# Enable only markdown
uv run python scripts/generate_build_config.py --enable markdown

# Enable KOReader sync (auto-enables integrations base)
uv run python scripts/generate_build_config.py --enable koreader_sync
```

**List available features:**
```bash
uv run python scripts/generate_build_config.py --list-features
```

**Profile naming behavior:** `--profile <name>` starts from that preset; adding `--enable` or `--disable` writes a generated `<profile>+overrides` custom profile to `platformio-custom.ini`. If you only pass feature toggles, the generated profile is `custom`. In the current generator, enabling `opendyslexic_fonts` in a generated custom profile also resolves both `bookerly_fonts` and `notosans_fonts`.

### Build and Flash

```bash
# Build the custom firmware
uv run pio run -e custom

# Build and flash in one command
uv run pio run -e custom --target upload

# Build and monitor serial output
uv run pio run -e custom --target upload --target monitor
```

### Verify Build Size

After building, check the firmware size:

```bash
# On Linux/macOS
ls -lh .pio/build/custom/firmware.bin

# Or use PlatformIO
uv run pio run -e custom -t size
```

---

## GitHub Actions Builds

GitHub Actions provides cloud-based builds without requiring local build tools.

### Using the Feature Picker

1. Go to [Feature Picker](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/)
2. Configure your features
3. Click "Build on GitHub Actions"
4. Sign in to GitHub if prompted
5. Run the workflow
6. Wait for the build (typically 5-10 minutes)
7. Download the artifact from the Actions page

> **Note on the Fork:** The web configurator and automated builds are primarily supported on the `fork-drift` branch of this fork. For more details on our branch management and relationship with upstream, see [docs/fork-strategy.md](fork-strategy.md).

### Manual Workflow Trigger

1. Go to your fork's Actions tab
2. Select "Build Custom Firmware" workflow
3. Click "Run workflow"
4. Select your branch
5. Choose profile or toggle individual features
6. Click "Run workflow"
7. Wait for completion
8. Download the generated custom-firmware artifact package

### Artifact Contents

The downloaded artifact contains:
- `firmware-YYYYMMDD-SHA.bin` - Flash this to your device
- `partitions.bin` - Partition table (usually not needed for OTA)
- `platformio-custom.ini` - Configuration used for this build
- `build-metadata.json` - Commit/profile/feature-set metadata for the package

---

## Flash Memory Considerations

### ESP32-C3 Flash Layout

The ESP32-C3 in the Xteink X4 has:
- **Total flash:** 16MB
- **Available for firmware:** ~6.4MB
- **Firmware partition:** 6MB (0x600000 bytes)
- **OTA partition:** 6MB (second firmware slot)

### Size Guidelines

| Build Type | Size | Flash Usage | Books Space |
|------------|------|-------------|-------------|
| Lean | ~1.7MB | 27% | Maximum |
| Standard | ~5.0MB | 78% | Good |
| Full | ~5.9MB | 92% | Tight |

*Note: Full build currently fits, but leaves very little headroom. Test before deploying.

### Tips for Managing Flash Space

1. **Start with Standard profile** - best balance for most users
2. **Disable unused features** - save space for more books
3. **Use BMP sleep images** - if you don't need PNG/JPEG
4. **Skip OpenDyslexic first** - it is the largest single optional font pack at ~2.6MB
5. **Monitor OTA updates** - custom builds may be larger than default

---

## Troubleshooting

### Build Fails with "firmware.bin is too large"

**Problem:** The configured features result in firmware larger than the partition.

**Solutions:**
1. Disable one or more features
2. Use a smaller profile (Standard instead of Full)
3. Specifically disable large optional packs such as OpenDyslexic or BLE provisioning

Example:
```bash
uv run python scripts/generate_build_config.py --profile standard
```

### Feature Not Working After Flash

**Problem:** A feature you expected to be enabled isn't working.

**Check:**
1. Verify the feature was enabled in `platformio-custom.ini`
2. Rebuild and flash again
3. Clear device settings (may have cached old behavior)

### Markdown Files Show Error

**Problem:** "Markdown support not available" message appears.

**Cause:** `ENABLE_MARKDOWN=0` in your build.

**Solution:** Rebuild with Markdown enabled:
```bash
uv run python scripts/generate_build_config.py --enable markdown
uv run pio run -e custom --target upload
```

### Sleep Images Not Loading

**Problem:** PNG/JPEG sleep images not displaying.

**Cause:** `ENABLE_IMAGE_SLEEP=0` in your build.

**Solutions:**
1. Rebuild with image sleep enabled:
   ```bash
   uv run python scripts/generate_build_config.py --enable image_sleep
   uv run pio run -e custom --target upload
   ```
2. Or convert your images to BMP format (works in all builds)

### GitHub Actions Build Fails

**Problem:** Workflow fails during build.

**Common causes:**
1. Invalid feature combination (rare)
2. Branch has compilation errors
3. Repository not properly forked

**Solutions:**
1. Check the Actions log for specific errors
2. Try a known-good profile (standard)
3. Ensure your fork is up to date with upstream

### How to Check Current Build Configuration

Current ways to inspect the active feature set:

1. Query `GET /api/plugins` (or the compatibility alias `GET /api/features`) while the web server is running
2. Check your local `platformio-custom.ini` file
3. Check the GitHub Actions build summary or `build-metadata.json` from a custom build artifact

---

## Advanced Usage

### Modifying Build Flags Directly

If you need fine-grained control, edit `platformio-custom.ini` directly:

```ini
[env:custom]
extends = base
build_flags =
  ${base.build_flags}
  -DCROSSPOINT_VERSION="${crosspoint.version}-custom"
  -DENABLE_BOOKERLY_FONTS=1
  -DENABLE_NOTOSANS_FONTS=1
  -DENABLE_IMAGE_SLEEP=1
  -DENABLE_BOOK_IMAGES=1
  -DENABLE_MARKDOWN=0
  -DENABLE_INTEGRATIONS=0
  -DENABLE_KOREADER_SYNC=0
  -DENABLE_CALIBRE_SYNC=0
  -DENABLE_BACKGROUND_SERVER=0
  -DENABLE_HOME_MEDIA_PICKER=1
  -DENABLE_POKEMON_WALLPAPER_PLUGIN=0
```

### Adding Your Own Feature Flags

To add a new optional feature:

1. Add the flag definition in your code:
   ```cpp
   #ifndef ENABLE_MY_FEATURE
   #define ENABLE_MY_FEATURE 1
   #endif
   ```

2. Wrap the feature code:
   ```cpp
   #if ENABLE_MY_FEATURE
   // Feature implementation
   #endif
   ```

3. Add to `generate_build_config.py`:
   ```python
   'my_feature': Feature(
       name='My Feature',
       flag='ENABLE_MY_FEATURE',
       size_kb=100,
       description='My custom feature'
   )
   ```

4. Update profiles as needed

---

## Related Documentation

- [README.md](../README.md) - Main project documentation
- [USER_GUIDE.md](../USER_GUIDE.md) - User guide for operating CrossPoint
- [Feature Picker](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/) - Web-based configuration tool

---

**Questions or Issues?**

- Check [Troubleshooting](#troubleshooting) section above
- Open an issue on GitHub
- Consult the main [README.md](../README.md)

---

## Feature Store OTA Catalog

The Feature Store provides over-the-air firmware bundles with different feature configurations. The catalog is a JSON file hosted alongside release artifacts.

### Catalog Format

Each bundle entry contains:
- `id` — Unique bundle identifier (e.g., `latest-standard`)
- `displayName` — Human-readable name shown in the OTA picker UI
- `version` — Version tag or `latest`/`nightly`
- `board` — Target board (must match device, e.g., `esp32c3`)
- `featureFlags` — Comma-separated compile-time feature flags included in the bundle
- `downloadUrl` — Direct URL to the firmware binary
- `checksum` — SHA-256 of the binary (empty string if not yet computed)
- `binarySize` — Size in bytes (0 if not yet known)

### Compatibility Checks

Before offering a bundle for installation, the device checks:
1. **Board match** — `bundle.board` must equal the device's configured board
2. **Partition size** — `bundle.binarySize` (if non-zero) must fit in the OTA partition

Incompatible bundles are shown in the picker with a warning and cannot be selected.

### Catalog Location

The catalog JSON is stored at `docs/ota/feature-store-catalog.json` in the repository and served from the GitHub releases page at runtime.

---

**Last Updated:** 2026-03-24

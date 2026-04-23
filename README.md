# CrossPoint Reader - UNSTABLE FORK DRIFT - VERY EXPERIMENTAL

Firmware for the **Xteink X4** e-paper display reader (unaffiliated with Xteink).
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

This is the **`fork-drift`** branch. It tracks upstream while adding fork-specific capabilities (notably the web build configurator and modular feature profiles).

[![Build](https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader/actions/workflows/build.yml/badge.svg?branch=fork-drift)](https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader/actions/workflows/build.yml)

<img width="959" height="1008" alt="image" src="https://github.com/user-attachments/assets/b787388b-9dc6-43eb-9fcb-24b4d8fee604" />


## Build Configurator

The **[Build Configurator](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/)** is the primary entry point for this fork.

Because the ESP32-C3 has limited flash and RAM, the configurator lets you choose only the features you need and generates matching build flags.

- Presets: `Lean`, `Standard`, `Full`
- Per-feature toggles with dependency enforcement
- Flash-size budgeting and generated `platformio-custom.ini`
- Direct bridge to forked web flasher/debug tools

## Fork-Drift Additions vs Upstream

Compared with `upstream/master`, this branch adds fork-specific user-facing capabilities:

- Web Build Configurator UI (`docs/configurator`) for custom firmware composition
- Modular compile-time feature flag system (`include/FeatureFlags.h`, `src/core/features/FeatureCatalog.h`)
- Markdown/Obsidian reader pipeline (`lib/Markdown`, `MarkdownReaderActivity`)
- TODO planner activities and daily storage flow (`src/activities/todo`)
- Background web server runtime modes (`src/network/BackgroundWebServer.*`)
- Web Wi-Fi setup + BLE provisioning flow (`src/network/BleWifiProvisioner.*`)
- USB Mass Storage mode integration (`ENABLE_USB_MASS_STORAGE`)
- User font pipeline (`UserFontManager` + CPF tooling under `tools/font-converter/`)
- Home UI variants exposed via configurator (Home Media Picker / Lyra / Visual Covers)
- Fork plugin surfaces (including web Pokedex plugin page)
- Fork release-channel/configuration docs and OTA catalog metadata (`docs/ota`, configurator docs)
- **Captive portal in AP/hotspot mode** — when the device creates a hotspot, any unrecognised HTTP request is redirected to the home page (302) rather than returning 404, triggering the OS "Sign in to network" notification on iOS, Android, and Windows automatically (see [`docs/fork-strategy.md`](./docs/fork-strategy.md#behavioral-drifts))

## Fork Branch Strategy

This repository maintains dedicated branches for its goals:

- `master`: Stable release branch for the **Fork Drift** firmware.
- `fork-drift`: Active development for experimental features and rapid iteration.
- `upstream-sync`: (Internal) Maintains direct parity with `crosspoint-reader/master` for merge-back flows.

For the full rationale, see [`docs/fork-strategy.md`](./docs/fork-strategy.md).

## Features & Usage

- [x] EPUB parsing and rendering (EPUB 2 and EPUB 3)
- [x] Image support within EPUB
- [x] Saved reading position
- [x] File explorer with file picker
  - [x] Basic EPUB picker from root directory
  - [x] Support nested folders
  - [ ] EPUB picker with cover art
- [x] Custom sleep screen
  - [x] Cover sleep screen
- [x] Wifi book upload
- [x] Wifi OTA updates
- [x] KOReader Sync integration for cross-device reading progress
- [x] Configurable font, layout, and display options
  - [ ] User provided fonts
  - [ ] Full UTF support
- [x] Screen rotation

Multi-language support: Read EPUBs in various languages, including English, Spanish, French, German, Italian, Portuguese, Russian, Ukrainian, Polish, Swedish, Norwegian, [and more](./USER_GUIDE.md#supported-languages).

See [the user guide](./USER_GUIDE.md) for instructions on operating CrossPoint, including the
[KOReader Sync quick setup](./USER_GUIDE.md#365-koreader-sync-quick-setup).

For scope/constraints, see [SCOPE.md](SCOPE.md).

## Installing

### Web Configurator (Recommended)

1. Go to the **[Build Configurator](https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/)**.
2. Select features and click **Build on GitHub**.
3. Once the build completes, download the `firmware.bin` or use the browser-based flasher.

### Quick Flash (Latest Stable)

1. Connect your Xteink X4 to your computer via USB-C.
2. Go to [xteink.dve.al](https://xteink.dve.al/) and click **"Flash CrossPoint firmware"**.

To revert to official firmware, use the "Swap boot partition" button at [xteink.dve.al/debug](https://xteink.dve.al/debug).

### Firmware Release Channels

### Command line (specific firmware version)

1. Install [`esptool`](https://github.com/espressif/esptool) :
```bash
pip install esptool
```
2. Download the `firmware.bin` file from the release of your choice via the [releases page](https://github.com/crosspoint-reader/crosspoint-reader/releases)
3. Connect your Xteink X4 to your computer via USB-C.
4. Note the device location. On Linux, run `dmesg` after connecting. On MacOS, run :
```bash
log stream --predicate 'subsystem == "com.apple.iokit"' --info
```
5. Flash the firmware :
```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```
Change `/dev/ttyACM0` to the device for your system.

Firmware is published as two rolling channel tags:

- `latest` (auto-updated from green `fork-drift` pushes)
- `stable` (updated on stable version tags)

### Manual

Each channel publishes five firmware variants plus one shared partitions file:

- `crosspoint-standard.bin`
- `crosspoint-lean.bin`
- `crosspoint-full.bin`
- `crosspoint-lean-reset.bin`
- `crosspoint-full-reset.bin`
- `crosspoint-partitions.bin`

### Web (Specific Firmware Version)

1. Connect your Xteink X4 via USB-C
2. Download `firmware.bin` from the [releases page](https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader/releases)
3. Flash it via [xteink.dve.al](https://xteink.dve.al/) (OTA fast flash controls)

You can also copy `firmware.bin` to the SD card root. On boot, the reader now shows a vertical menu to install it, skip for now, skip that firmware version, or delete the file.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

CrossPoint uses PlatformIO for building and flashing the firmware. To get started, clone the repository:

```
# Clone the fork-drift branch (active development)
git clone --recursive --branch fork-drift https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Devcontainer and local CI

This repo now ships with a devcontainer that installs the toolchain used by the online workflows and a local CI runner that mirrors the same workflow commands.

```sh
# Open the repository in the devcontainer, then run:
scripts/ci/run-local.sh ci-build
scripts/ci/run-local.sh build-workflow
scripts/ci/run-local.sh profile-matrix
scripts/ci/run-local.sh update-screen-previews
```

The local runner intentionally skips GitHub-only steps such as artifact reuse, artifact upload, and release publishing, but it uses the same build, test, and generation commands as the corresponding workflows under `.github/workflows/`.

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
pio run --target upload
```
### Debugging

After flashing the new features, it’s recommended to capture detailed logs from the serial port.

For a simple local workflow that uploads first, then starts the monitor and saves
everything to a timestamped log file under `.logs/serial/`, use:

```sh
./scripts/pio_debug.sh
```

This is the more practical option when you are connected over SSH or Cursor,
because the serial output is also persisted to disk for later inspection.

Useful variants:

```sh
# Skip flashing and just attach the monitor
./scripts/pio_debug.sh --no-upload

# Pin a specific serial device
./scripts/pio_debug.sh --port /dev/ttyACM0
```

First, make sure all required Python packages are installed:

```python
python3 -m pip install pyserial colorama matplotlib
```
after that run the script:
```sh
# For Linux
# This was tested on Debian and should work on most Linux systems.
python3 scripts/debugging_monitor.py

# For macOS
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```
Minor adjustments may be required for Windows.

## Internals

CrossPoint Reader is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only
has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based
on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the 
cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:


```
.crosspoint/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache. 

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book
file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Contributing

Contributions are very welcome!

If you are new to the codebase, start with the [contributing docs](./docs/contributing/README.md).

If you're looking for a way to help out, take a look at the [ideas discussion board](https://github.com/crosspoint-reader/crosspoint-reader/discussions/categories/ideas).
If there's something there you'd like to work on, leave a comment so that we can avoid duplicated effort.

Everyone here is a volunteer, so please be respectful and patient. For more details on our governance and community
principles, please see [GOVERNANCE.md](GOVERNANCE.md).

### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/dithering-improvement`)
3. Make changes
4. Submit a PR

---

CrossPoint Reader is **not affiliated with Xteink or any manufacturer of the X4 hardware**.

Huge shoutout to [**diy-esp32-epub-reader** by atomic14](https://github.com/atomic14/diy-esp32-epub-reader), which was a project I took a lot of inspiration from as I
was making CrossPoint.

Shoutout also to [**BOOX-Pokedex-Wallpaper-Generator** by m86-tech](https://github.com/m86-tech/BOOX-Pokedex-Wallpaper-Generator),
the upstream project behind CrossPoint's Pokedex companion plugin. Go check it out if you want Pokédex wallpapers on
any E-Ink device — it supports Boox, Remarkable, Kindle, Kobo, and more.

# ForkDrift Configurator Test Plan

## Pre-Build Validation

- [x] Configuration generator script runs without errors
- [x] All profiles generate valid configurations (lean, standard, full)
- [x] Custom feature selection works
- [x] Profile overrides work (e.g., `--profile full --disable markdown`)
- [x] Feature size estimates are refreshed from the nightly measurement data
- [x] Build flags are formatted correctly in output

## Build Tests

### Lean Profile Build

```bash
uv run python scripts/generate_build_config.py --profile lean
uv run pio run -e custom
```

**Expected:**
- [  ] Build completes successfully
- [  ] Firmware size is close to the current script estimate (~2.4MB)
- [  ] No compilation errors
- [  ] All optional feature flags are set to 0

**Verify on device:**
- [  ] TXT reading works
- [  ] BMP sleep screens work
- [  ] PNG/JPEG sleep screens are ignored or show error
- [  ] Opening .md files shows "not supported" message
- [  ] Optional web/integration surfaces are absent

### Standard Profile Build

```bash
uv run python scripts/generate_build_config.py --profile standard
uv run pio run -e custom
```

**Expected:**
- [  ] Build completes successfully
- [  ] Firmware size is close to the current script estimate (~5.6MB)
- [  ] No compilation errors
- [  ] Bookerly, Noto Sans, EPUB, hyphenation, image sleep, user fonts, BLE WiFi provisioning, USB mass storage, WiFi clock, and sleep clock screens are enabled

**Verify on device:**
- [  ] EPUB reading works with multiple Bookerly/Noto Sans sizes
- [  ] PNG/JPEG sleep screens work
- [  ] BMP sleep screens work
- [  ] Opening .md files shows "not supported" message
- [  ] `/api/plugins` reports the standard feature set accurately

### Full Profile Build

```bash
uv run python scripts/generate_build_config.py --profile full
uv run pio run -e custom
```

**Expected:**
- [  ] Build completes successfully
- [  ] Firmware size is close to the current script estimate (~5.6MB)
- [  ] No compilation errors
- [  ] Full-profile features are enabled, including Lexend Deca and Chare Ink fonts instead of Bookerly/Noto Sans

**Verify on device:**
- [  ] Lexend Deca and Chare Ink font packs work
- [  ] PNG/JPEG/BMP sleep screens work
- [  ] Markdown files render correctly
- [  ] Obsidian features work (wikilinks, callouts, etc.)
- [  ] Background web server stays running while reading
- [  ] WiFi file upload works during reading

### Custom Build Tests

#### Test: Bookerly + Noto Sans Fonts Only
```bash
uv run python scripts/generate_build_config.py --enable bookerly_fonts --enable notosans_fonts
uv run pio run -e custom
```

**Verify:**
- [  ] Multiple font sizes available
- [  ] PNG/JPEG sleep images don't work
- [  ] Markdown files show error

#### Test: Markdown Only
```bash
uv run python scripts/generate_build_config.py --enable markdown
uv run pio run -e custom
```

**Verify:**
- [  ] Markdown files render
- [  ] Only 14pt font available
- [  ] PNG/JPEG sleep images don't work

## GitHub Actions Build Test

1. [  ] Push changes to fork (ensure you are on the `fork-drift` branch)
2. [  ] Navigate to Actions tab
3. [  ] Run "Build Custom Firmware" workflow
4. [  ] Select "standard" profile
5. [  ] Wait for build completion
6. [  ] Download artifact
7. [  ] Verify artifact contains:
   - firmware-YYYYMMDD-SHA.bin
   - partitions.bin
   - platformio-custom.ini
   - build-metadata.json
8. [  ] Flash the named firmware file to device
9. [  ] Verify features match standard profile

> **Note:** Most active development and build testing should be performed on the `fork-drift` branch. See [fork-strategy.md](fork-strategy.md) for more details.

## ForkDrift Configurator Web UI Test

### Published (GitHub Pages)

1. [  ] Open https://unintendedsideeffects.github.io/ForkDrift-crosspointReader/configurator/
2. [  ] Page loads without errors
3. [  ] Profile buttons work
4. [  ] Individual feature toggles work
5. [  ] Size estimate updates in real-time
6. [  ] Size bar visual updates correctly
7. [  ] "Build on GitHub Actions" button opens correct URL
8. [  ] URL includes selected features as query parameters
9. [  ] Test on mobile device (responsive design)
10. [  ] Test on desktop browser

### Local (browser-sync)

1. [  ] From `crosspoint-reader/docs/configurator`: `npm install && npm run dev`
2. [  ] Open http://localhost:3000/configurator/ (or LAN URL from browser-sync)
3. [  ] Live reload works when editing `index.html`
4. [  ] Settings preview reflects `settings-schema.generated.js` (run `npm run schema:check` after firmware settings changes)
5. [  ] Screen preview tab loads `screen-previews/manifest.json` (PNG assets present after harness run)

## Graceful Degradation Tests

### Markdown Disabled

**Test opening .md file:**
- [  ] Shows clear error message "Markdown support not available"
- [  ] Doesn't crash
- [  ] Can navigate back to library

**Test Todo feature:**
- [  ] Defaults to .txt format instead of .md
- [  ] Todo files can be created and edited
- [  ] No crashes or errors

### Image Sleep Disabled

**Test with PNG sleep image:**
- [  ] PNG files in /sleep/ folder are ignored
- [  ] Falls back to BMP or default sleep screen
- [  ] No crashes

**Test with BMP sleep image:**
- [  ] BMP files work normally
- [  ] Sleep screen displays correctly

### Bookerly/Noto Sans Disabled

**Test font settings:**
- [  ] Disabled font families do not appear as selectable reader fonts
- [  ] The remaining built-in families still work
- [  ] Generated custom profiles that enable OpenDyslexic also resolve at least one qualifying parent font pack

### Background Server Disabled

**Test reading mode:**
- [  ] Web server stops when entering reader
- [  ] Can't access web interface while reading
- [  ] Web server restarts when exiting reader
- [  ] No crashes

## Documentation Tests

- [  ] README.md "Custom Builds" section is clear
- [  ] BUILD_CONFIGURATION.md is comprehensive
- [  ] All links work
- [  ] Code examples are correct
- [  ] ForkDrift Configurator link is correct
- [  ] No typos or formatting errors

## Regression Tests

Run with default build (`uv run pio run -e default`) to ensure no breakage:

- [  ] All features work as before
- [  ] No new compilation warnings
- [  ] No size increase in default build
- [  ] Host tests pass (`bash test/run_host_tests.sh`)

## Performance Tests

- [  ] Lean profile build: Measure firmware size reduction
- [  ] Standard build: Verify boot time not impacted
- [  ] Full build: Ensure no memory issues
- [  ] Compare free heap between builds

## Known Issues / Notes

- Full profile has tight headroom on the 6.4MB partition - verify on hardware
- Runtime feature detection is available via `/api/plugins`
- Web configurator assumes standard GitHub repository structure

## Sign-Off

**Date:**
**Tested by:**
**Build commit:**
**Result:** ☐ Pass  ☐ Fail  ☐ Conditional Pass

**Notes:**

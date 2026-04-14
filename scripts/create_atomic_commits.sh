#!/bin/bash
# Create atomic commits for feature picker integration
# Usage: ./scripts/create_atomic_commits.sh

set -e

echo "🔧 Creating atomic commits for Feature Picker integration"
echo "=========================================================="
echo ""

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if on correct branch
CURRENT_BRANCH=$(git branch --show-current)
echo "Current branch: ${BLUE}${CURRENT_BRANCH}${NC}"
echo ""

read -p "⚠️  This will create multiple commits. Continue? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

echo ""
echo "${GREEN}Phase 1: Foundation${NC}"
echo "-------------------"

# Component 1: Core Infrastructure
echo "${BLUE}[1/10]${NC} Core infrastructure..."
git add scripts/generate_build_config.py
git commit -m "feat(build): add build configuration generator script

- Add Python script to generate custom PlatformIO configs
- Define feature flags: ENABLE_EXTENDED_FONTS, ENABLE_IMAGE_SLEEP, ENABLE_MARKDOWN, ENABLE_BACKGROUND_SERVER
- Support profiles: lean, standard, full
- Calculate firmware size estimates
- Generate platformio-custom.ini with custom build flags

Component: Core Infrastructure
Dependencies: None
Size Impact: N/A (tool only)
Breaking: No
Default Behavior: Unchanged"

echo ""
echo "${GREEN}Phase 2: Feature Flags${NC}"
echo "----------------------"

# Component 2a: Extended Fonts
echo "${BLUE}[2/10]${NC} Extended fonts feature flag..."
git add src/main.cpp
git commit -m "feat(fonts): add ENABLE_EXTENDED_FONTS feature flag

Replace OMIT_FONTS with ENABLE_EXTENDED_FONTS for consistency.

When enabled (default):
- Bookerly 12, 16, 18pt fonts
- Noto Sans 12, 14, 16, 18pt fonts
- OpenDyslexic 8, 10, 12, 14pt fonts

When disabled:
- Only Bookerly 14pt (default reading)
- Ubuntu UI fonts
- Noto Sans 8pt (small text)

Component: Feature Flag 2a
Dependencies: Component 1
Size Impact: ~300KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

# Component 2b: Image Sleep
echo "${BLUE}[3/10]${NC} Image sleep feature flag (part 1/3)..."
git add lib/Epub/Epub/converters/ImageDecoderFactory.h
git commit -m "feat(sleep): add ENABLE_IMAGE_SLEEP flag to ImageDecoderFactory.h

Guard PNG/JPEG decoder declarations with ENABLE_IMAGE_SLEEP flag.
BMP support remains always-on via display library.

Component: Feature Flag 2b (1/3)
Dependencies: Component 1
Size Impact: ~140KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

echo "${BLUE}[4/10]${NC} Image sleep feature flag (part 2/3)..."
git add lib/Epub/Epub/converters/ImageDecoderFactory.cpp
git commit -m "feat(sleep): add ENABLE_IMAGE_SLEEP flag to ImageDecoderFactory.cpp

Conditionally compile PNG/JPEG decoders:
- PNGdec library (PNG support)
- picojpeg library (JPEG support)

When disabled, only BMP format supported (via display library).
getSupportedFormats() returns appropriate extensions list.

Component: Feature Flag 2b (2/3)
Dependencies: Component 1, 2b.1
Size Impact: ~140KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

echo "${BLUE}[5/10]${NC} Image sleep feature flag (part 3/3)..."
git add src/activities/boot_sleep/SleepActivity.cpp
git commit -m "feat(sleep): update sleep image extensions with ENABLE_IMAGE_SLEEP

SLEEP_IMAGE_EXTENSIONS array now conditional:
- When enabled: [\".bmp\", \".png\", \".jpg\", \".jpeg\"]
- When disabled: [\".bmp\"] (BMP only)

Component: Feature Flag 2b (3/3)
Dependencies: Component 1, 2b.1, 2b.2
Size Impact: ~140KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

# Component 2c: Markdown
echo "${BLUE}[6/10]${NC} Markdown feature flag (part 1/3)..."
git add src/activities/reader/ReaderActivity.h
git commit -m "feat(markdown): add ENABLE_MARKDOWN flag to ReaderActivity.h

Guard Markdown class declaration and method signatures.

When disabled:
- loadMarkdown() method not declared
- onGoToMarkdownReader() method not declared
- Markdown class forward declaration guarded

Component: Feature Flag 2c (1/3)
Dependencies: Component 1
Size Impact: ~560KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

echo "${BLUE}[7/10]${NC} Markdown feature flag (part 2/3)..."
git add src/activities/reader/ReaderActivity.cpp
git commit -m "feat(markdown): add ENABLE_MARKDOWN flag to ReaderActivity.cpp

Conditionally compile Markdown support:
- Guard #include \"Markdown.h\" and \"MarkdownReaderActivity.h\"
- Guard loadMarkdown() implementation
- Guard onGoToMarkdownReader() implementation
- Show user-friendly error when .md file opened with flag disabled

When disabled, opening .md files shows:
\"Markdown support not available in this build\"

Component: Feature Flag 2c (2/3)
Dependencies: Component 1, 2c.1
Size Impact: ~560KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

echo "${BLUE}[8/10]${NC} Markdown feature flag (part 3/3)..."
git add src/main.cpp
git commit --amend --no-edit || git commit -m "feat(todo): make Todo feature respect ENABLE_MARKDOWN flag

When ENABLE_MARKDOWN disabled:
- Skip checking for .md todo files
- Default to .txt format for new todos
- Existing behavior unchanged when enabled

Component: Feature Flag 2c (3/3)
Dependencies: Component 1, 2c.1, 2c.2
Size Impact: ~560KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

# Component 2d: Background Server
echo "${BLUE}[9/10]${NC} Background server feature flag..."
git add src/network/BackgroundWebServer.cpp
git commit -m "feat(server): add ENABLE_BACKGROUND_SERVER feature flag

Make background web server optional.

When enabled (default):
- Server runs in background during reading
- WiFi stays connected while reading
- File uploads possible during reading

When disabled:
- loop() returns immediately (no-op)
- Server only runs in Home/Library views
- Minimal memory impact

Component: Feature Flag 2d
Dependencies: Component 1
Size Impact: ~5KB when disabled
Breaking: No
Default: Enabled (1)" || echo "Already committed or no changes"

echo ""
echo "${GREEN}Phase 3: Build System${NC}"
echo "--------------------"

# Component 3a: GitHub Actions
echo "${BLUE}[10/10]${NC} GitHub Actions workflow..."
git add .github/workflows/build-custom.yml
git commit -m "feat(ci): add custom firmware build workflow

Add workflow_dispatch workflow for building custom firmware:
- Feature toggles for each optional component
- Profile selection (lean/standard/full)
- Automatic build configuration generation
- Firmware artifact upload
- Build size reporting in job summary

Usage:
1. Go to Actions tab
2. Select \"Build Custom Firmware\"
3. Choose profile or toggle features
4. Run workflow
5. Download custom-firmware artifact

Component: GitHub Actions Workflow
Dependencies: All feature flags (2a-2d)
Size Impact: N/A (CI only)
Breaking: No" || echo "Already committed or no changes"

echo ""
echo "${GREEN}Phase 4: User Tools${NC}"
echo "------------------"

# Component 3b: Web Configurator
if [ -d "docs/configurator" ]; then
    echo "Adding web configurator..."
    git add docs/configurator/
    git commit -m "feat(web): add Feature Picker web configurator

Static web UI for custom firmware configuration:
- Interactive feature toggles
- Real-time size calculator
- Profile buttons (lean/standard/full)
- Build flag generation and display
- platformio-custom.ini download
- Direct link to GitHub Actions workflow

Hosted on GitHub Pages at:
https://[username].github.io/ForkDrift-crosspointReader/configurator/

Component: Web Configurator UI
Dependencies: Component 1, 3a
Size Impact: N/A (web only)
Breaking: No" || echo "Already committed or no changes"
fi

echo ""
echo "${GREEN}Phase 5: Documentation${NC}"
echo "----------------------"

# Component 4: Documentation
echo "Adding documentation..."
git add README.md docs/BUILD_CONFIGURATION.md docs/FEATURE_PICKER_TEST_PLAN.md docs/MODULAR_ARCHITECTURE_GUIDE.md 2>/dev/null || true
git commit -m "docs: add Feature Picker documentation

Comprehensive documentation for custom firmware builds:

README.md:
- Add \"Custom Builds\" section
- Feature table with size impacts
- Preset descriptions
- Quick start instructions

BUILD_CONFIGURATION.md:
- Detailed feature reference
- Local build instructions
- GitHub Actions guide
- Troubleshooting section
- Flash memory considerations

FEATURE_PICKER_TEST_PLAN.md:
- Comprehensive test checklist
- Build validation tests
- On-device verification
- Graceful degradation tests

Component: Documentation
Dependencies: All components
Size Impact: N/A (docs only)
Breaking: No" || echo "Already committed or no changes"

echo ""
echo "${GREEN}✅ Done!${NC}"
echo ""
echo "Created atomic commits for Feature Picker integration."
echo ""
echo "Next steps:"
echo "1. Review commits: ${BLUE}git log --oneline -15${NC}"
echo "2. Push to remote: ${BLUE}git push origin ${CURRENT_BRANCH}${NC}"
echo "3. Create PRs for each phase or merge all at once"
echo ""
echo "Architecture guide: ${BLUE}docs/MODULAR_ARCHITECTURE_GUIDE.md${NC}"

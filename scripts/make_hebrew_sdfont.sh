#!/usr/bin/env bash
# Generate a Hebrew SD-card font family (NotoHebrew) for RTL reading.
#
# The firmware's RTL support (bidi layout) is flash-resident, but Hebrew glyph
# coverage is not: at ~88% flash there is no room for more built-in font data.
# This generates the coverage as an SD font instead (~40KB on the card).
#
# Usage:  scripts/make_hebrew_sdfont.sh [output-dir]
#         Copy the resulting NotoHebrew/ directory into /fonts/ on the SD card,
#         then pick it via Settings -> Reading -> Font Family -> External.
#
# Note: Arabic is intentionally not offered — it requires contextual glyph
# shaping the renderer does not implement; Hebrew is non-joining and works.
set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-./NotoHebrew}"
SRC_DIR="$(mktemp -d)"
trap 'rm -rf "$SRC_DIR"' EXIT

BASE="https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSansHebrew/hinted/ttf"
echo "Downloading Noto Sans Hebrew (OFL licensed)..."
curl -sL -o "$SRC_DIR/Regular.ttf" "$BASE/NotoSansHebrew-Regular.ttf"
curl -sL -o "$SRC_DIR/Bold.ttf" "$BASE/NotoSansHebrew-Bold.ttf"

mkdir -p "$OUT"
uv run --with fonttools --with freetype-py --with pyyaml \
  python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
  --regular "$SRC_DIR/Regular.ttf" --bold "$SRC_DIR/Bold.ttf" \
  --intervals ascii,hebrew --sizes 12,14,16,18 \
  --name NotoHebrew --output-dir "$OUT"

echo
echo "Done: copy '$OUT' into the SD card's /fonts/ directory."

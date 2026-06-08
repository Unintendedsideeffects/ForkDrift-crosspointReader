#!/usr/bin/env bash
# Local, on-demand release: build the firmware profiles here and publish them to a
# GitHub Release, so OTA delivery never consumes GitHub Actions minutes. This is
# the local equivalent of the (now push-disabled) build.yml `publish-latest` job.
#
# Usage:
#   scripts/release.sh [channel]
#     channel: latest | nightly | stable   (default: latest)
#
# It builds three profiles and uploads the channel's asset set, matching what the
# device expects on both OTA paths:
#   - Feature store (Settings > OTA picker): crosspoint-{lean,standard,full}.bin
#     + the -reset variants + crosspoint-partitions.bin (see feature-store-catalog.json)
#   - Release channel fallback: firmware-<YYYYMMDD>-<sha>.bin (standard build)
# Release title = "<commit-count>-dev", which the device compares to decide "newer".
#
# After publishing it refreshes binarySize in docs/ota/feature-store-catalog.json;
# commit + push that file so devices see accurate sizes (it is served from the
# fork-drift branch, not the release).
set -euo pipefail

CHANNEL="${1:-latest}"
REPO="Unintendedsideeffects/ForkDrift-crosspointReader"
case "$CHANNEL" in
  latest | nightly | stable) ;;
  *) echo "error: channel must be latest | nightly | stable" >&2; exit 1 ;;
esac

cd "$(dirname "$0")/.."
BUILD_DIR="${HOME}/.cache/crosspoint-pio-build"
STAGE="$(mktemp -d)"
CHAN_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE" "$CHAN_DIR"' EXIT

# Version that the device's OTA compares against the installed build.
COUNT="$(git rev-list --count HEAD)"
[ "$COUNT" -gt 99999 ] && COUNT=99999
VERSION="${COUNT}-dev"
echo ">> Release channel '$CHANNEL', version $VERSION"

stage_built_bin() {  # $1 = destination path in STAGE
  local src
  src="$(readlink -f firmware-latest.bin || true)"
  if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "error: built firmware not found via firmware-latest.bin" >&2
    exit 1
  fi
  cp "$src" "$1"
}

echo ">> [1/3] Building standard (gh_latest)..."
uv run pio run -e gh_latest
STD_NAME="$(basename "$(readlink -f firmware-latest.bin)")"  # firmware-<date>-<sha>.bin
stage_built_bin "$STAGE/standard.bin"
cp "$BUILD_DIR/gh_latest/partitions.bin" "$STAGE/partitions.bin"

echo ">> [2/3] Building lean (custom profile)..."
uv run python scripts/generate_build_config.py --profile lean >/dev/null
sed -i 's/-custom"/-lean"/g' platformio-custom.ini
uv run pio run -e custom
stage_built_bin "$STAGE/lean.bin"

echo ">> [3/3] Building full (custom profile)..."
uv run python scripts/generate_build_config.py --profile full >/dev/null
sed -i 's/-custom"/-full"/g' platformio-custom.ini
uv run pio run -e custom
stage_built_bin "$STAGE/full.bin"

# --- Assemble the channel's asset set (mirrors publish-latest) ---
cp "$STAGE/standard.bin"   "$CHAN_DIR/$STD_NAME"
cp "$STAGE/standard.bin"   "$CHAN_DIR/crosspoint-standard.bin"
cp "$STAGE/partitions.bin" "$CHAN_DIR/crosspoint-partitions.bin"
cp "$STAGE/lean.bin"       "$CHAN_DIR/crosspoint-lean.bin"
cp "$STAGE/full.bin"       "$CHAN_DIR/crosspoint-full.bin"
cp "$STAGE/lean.bin"       "$CHAN_DIR/crosspoint-lean-reset.bin"
cp "$STAGE/full.bin"       "$CHAN_DIR/crosspoint-full-reset.bin"
# The catalog's nightly bundle references this legacy name; keep it as a standard alias.
[ "$CHANNEL" = "nightly" ] && cp "$STAGE/standard.bin" "$CHAN_DIR/crosspoint-reader-nightly.bin"

echo ">> Built assets:"; ls -lh "$CHAN_DIR"

# --- Publish to the release ---
if ! gh release view "$CHANNEL" --repo "$REPO" >/dev/null 2>&1; then
  echo ">> Creating '$CHANNEL' release..."
  gh release create "$CHANNEL" --repo "$REPO" --prerelease --title "$VERSION" --notes "Local build $VERSION"
else
  gh release edit "$CHANNEL" --repo "$REPO" --title "$VERSION" >/dev/null
fi

echo ">> Removing existing assets on '$CHANNEL'..."
for asset in $(gh release view "$CHANNEL" --repo "$REPO" --json assets -q '.assets[].name'); do
  gh release delete-asset "$CHANNEL" "$asset" --repo "$REPO" --yes || true
done

echo ">> Uploading channel assets..."
gh release upload "$CHANNEL" "$CHAN_DIR"/* --repo "$REPO" --clobber

# --- Refresh feature-store catalog sizes for this channel's bundles ---
echo ">> Refreshing feature-store-catalog.json sizes for '$CHANNEL'..."
CHANNEL="$CHANNEL" CHAN_DIR="$CHAN_DIR" python3 - <<'PY'
import json, os
channel = os.environ["CHANNEL"]
chan_dir = os.environ["CHAN_DIR"]
path = "docs/ota/feature-store-catalog.json"
with open(path) as f:
    doc = json.load(f)
marker = f"/releases/download/{channel}/"
changed = 0
for b in doc.get("bundles", []):
    url = b.get("downloadUrl", "")
    if marker not in url:
        continue
    asset = url.rsplit("/", 1)[-1]
    local = os.path.join(chan_dir, asset)
    if os.path.isfile(local):
        b["binarySize"] = os.path.getsize(local)
        changed += 1
with open(path, "w") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")
print(f"   updated {changed} bundle size(s)")
PY

echo ">> Done. '$CHANNEL' (title $VERSION) published with the full profile set."
echo ">> NOTE: commit + push docs/ota/feature-store-catalog.json so devices see the new sizes."

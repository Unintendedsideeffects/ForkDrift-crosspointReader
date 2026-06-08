#!/usr/bin/env bash
# Local, on-demand release: build the firmware profiles here and publish them to
# GitHub Releases, so OTA delivery never consumes GitHub Actions minutes. This is
# the local equivalent of the (push-disabled) build.yml publish job.
#
# Usage:
#   scripts/release.sh [channel ...]
#     channel: latest | nightly | stable   (default: latest)
#   e.g. scripts/release.sh stable latest   -> canon = {stable,latest} x {lean,standard,full}
#
# The three profiles come from one commit, so they are compiled ONCE and the same
# bytes are published to every channel given (rebuilding identical bytes per
# channel would only waste time). Each channel gets the asset set the device
# expects on both OTA paths:
#   - Feature store (Settings > OTA picker): crosspoint-{lean,standard,full}.bin
#     + the -reset variants + crosspoint-partitions.bin (feature-store-catalog.json)
#   - Release channel fallback: firmware-<YYYYMMDD>-<sha>.bin (standard build)
# Release title = "<commit-count>-dev"; the device compares it to decide "newer".
#
# After publishing it refreshes binarySize in docs/ota/feature-store-catalog.json;
# commit + push that file so devices see accurate sizes (served from the branch).
set -uo pipefail

# --publish-only reuses already-staged profile bins (no rebuild), so a failed
# publish can be retried without recompiling.
PUBLISH_ONLY=0
CHANNELS=()
for a in "$@"; do
  case "$a" in
    --publish-only) PUBLISH_ONLY=1 ;;
    latest | nightly | stable) CHANNELS+=("$a") ;;
    *) echo "error: arg must be a channel (latest|nightly|stable) or --publish-only (got '$a')" >&2; exit 1 ;;
  esac
done
[ ${#CHANNELS[@]} -eq 0 ] && CHANNELS=("latest")
REPO="Unintendedsideeffects/ForkDrift-crosspointReader"

cd "$(dirname "$0")/.."
BUILD_DIR="${HOME}/.cache/crosspoint-pio-build"
STAGE="/tmp/forkdrift-release-stage"   # fixed dir so --publish-only can reuse it
mkdir -p "$STAGE/channel"

die() { echo "error: $*" >&2; exit 1; }

COUNT="$(git rev-list --count HEAD)"
[ "$COUNT" -gt 99999 ] && COUNT=99999
VERSION="${COUNT}-dev"
echo ">> Channels: ${CHANNELS[*]}   version: $VERSION"

stage_built_bin() {  # $1 = destination path
  local src
  src="$(readlink -f firmware-latest.bin || true)"
  [ -n "$src" ] && [ -f "$src" ] || die "built firmware not found via firmware-latest.bin"
  cp "$src" "$1" || die "failed to stage $1"
}

# Build one feature profile via the custom env. The naming post-script deletes
# the prior firmware/firmware-*.bin each run, so we stage immediately.
build_profile() {  # $1 = profile, $2 = version suffix (may be empty)
  echo ">> Building profile '$1'..."
  uv run python scripts/generate_build_config.py --profile "$1" >/dev/null || die "config gen failed ($1)"
  if [ -n "$2" ]; then
    sed -i "s/-custom\"/$2\"/g" platformio-custom.ini
  else
    sed -i 's/-custom"/"/g' platformio-custom.ini
  fi
  uv run pio run -e custom || die "build failed ($1)"
  stage_built_bin "$STAGE/$1.bin"
}

if [ "$PUBLISH_ONLY" -eq 1 ]; then
  echo ">> --publish-only: reusing staged bins in $STAGE"
  for f in lean.bin standard.bin full.bin partitions.bin std_name.txt; do
    [ -f "$STAGE/$f" ] || die "missing staged $f; run a full build first"
  done
  STD_NAME="$(cat "$STAGE/std_name.txt")"
else
  build_profile lean "-lean"
  build_profile standard ""
  STD_NAME="$(basename "$(readlink -f firmware-latest.bin)")"   # firmware-<date>-<time>-<sha>.bin
  echo "$STD_NAME" > "$STAGE/std_name.txt"
  cp "$BUILD_DIR/custom/partitions.bin" "$STAGE/partitions.bin" || die "missing partitions.bin"
  build_profile full "-full"
fi

# Assemble the per-channel asset set (mirrors build.yml publish-latest).
assemble_channel() {  # $1 = channel
  rm -f "$STAGE/channel"/*
  cp "$STAGE/standard.bin"   "$STAGE/channel/$STD_NAME"
  cp "$STAGE/standard.bin"   "$STAGE/channel/crosspoint-standard.bin"
  cp "$STAGE/partitions.bin" "$STAGE/channel/crosspoint-partitions.bin"
  cp "$STAGE/lean.bin"       "$STAGE/channel/crosspoint-lean.bin"
  cp "$STAGE/full.bin"       "$STAGE/channel/crosspoint-full.bin"
  cp "$STAGE/lean.bin"       "$STAGE/channel/crosspoint-lean-reset.bin"
  cp "$STAGE/full.bin"       "$STAGE/channel/crosspoint-full-reset.bin"
  if [ "$1" = "nightly" ]; then
    cp "$STAGE/standard.bin" "$STAGE/channel/crosspoint-reader-nightly.bin"
  fi
}

publish_channel() {  # $1 = channel
  local ch="$1"
  echo ">> Publishing '$ch'..."
  assemble_channel "$ch"
  if ! gh release view "$ch" --repo "$REPO" >/dev/null 2>&1; then
    gh release create "$ch" --repo "$REPO" --prerelease --title "$VERSION" --notes "Local build $VERSION" \
      || die "release create failed ($ch)"
  else
    gh release edit "$ch" --repo "$REPO" --title "$VERSION" >/dev/null || die "release edit failed ($ch)"
  fi
  local asset
  for asset in $(gh release view "$ch" --repo "$REPO" --json assets -q '.assets[].name'); do
    gh release delete-asset "$ch" "$asset" --repo "$REPO" --yes || true
  done
  gh release upload "$ch" "$STAGE/channel"/* --repo "$REPO" --clobber || die "asset upload failed ($ch)"
  echo ">> '$ch' published ($(ls "$STAGE/channel" | wc -l) assets)."
}

for ch in "${CHANNELS[@]}"; do
  publish_channel "$ch"
done

# Refresh feature-store catalog sizes for the published channels.
echo ">> Refreshing feature-store-catalog.json sizes..."
CHANNELS="${CHANNELS[*]}" STAGE="$STAGE" python3 - <<'PY'
import json, os
channels = os.environ["CHANNELS"].split()
chan_dir = os.path.join(os.environ["STAGE"], "channel")
path = "docs/ota/feature-store-catalog.json"
with open(path) as f:
    doc = json.load(f)
changed = 0
for b in doc.get("bundles", []):
    url = b.get("downloadUrl", "")
    if not any(f"/releases/download/{c}/" in url for c in channels):
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

echo ">> Done. Canon published to: ${CHANNELS[*]} (title $VERSION)."
echo ">> NOTE: commit + push docs/ota/feature-store-catalog.json so devices see new sizes."

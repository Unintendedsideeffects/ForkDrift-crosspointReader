#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

PY_BUILD="3.11"
PY_LINT="3.14"

ensure_python() {
  uv python install "$1" >/dev/null
}

sync_env() {
  local version="$1"
  ensure_python "$version"
  uv sync --frozen --python "$version"
}

ensure_pio_python() {
  local version="$1"
  uv run --python "$version" python -m ensurepip --upgrade || true
  uv run --python "$version" python -m pip --version >/dev/null
}

run_py() {
  local version="$1"
  shift
  uv run --python "$version" "$@"
}

run_clang_format_check() {
  sync_env "$PY_LINT"
  run_py "$PY_LINT" ./bin/clang-format-fix
  git diff --exit-code || {
    echo "Please run scripts/ci/run-local.sh ci-build after reviewing formatting changes." >&2
    return 1
  }
}

run_feature_sync_check() {
  sync_env "$PY_LINT"
  run_py "$PY_LINT" python scripts/check_feature_key_sync.py
  run_py "$PY_LINT" python scripts/generate_build_config.py --profile standard
  git diff --exit-code -- platformio-custom.ini
}

run_host_tests() {
  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  bash test/run_host_tests.sh
}

run_contract_server_check() {
  ensure_python "$PY_BUILD"
  python3 scripts/validate_contract_server.py
}

run_hardware_pattern_check() {
  ensure_python "$PY_LINT"
  run_py "$PY_LINT" python scripts/check_hardware_patterns.py
}

run_cleanup_checks() {
  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  run_py "$PY_BUILD" vulture scripts
  bash scripts/check_feature_boundaries.sh
  run_py "$PY_BUILD" pio check -e default
}

run_cppcheck() {
  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  run_py "$PY_BUILD" pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
}

validate_firmware_size() {
  local firmware_path="$1"
  local expected_max_mb="$2"
  local max_flash_mb="$3"

  if [[ ! -f "$firmware_path" ]]; then
    echo "firmware.bin not found at $firmware_path" >&2
    return 1
  fi

  local size_bytes size_mb
  size_bytes="$(stat -c%s "$firmware_path")"
  size_mb="$(SIZE_BYTES="$size_bytes" python3 - <<'PY'
import os
size = int(os.environ["SIZE_BYTES"])
print(size / (1024 * 1024))
PY
)"

  echo "Actual size:    ${size_mb}MB"
  echo "Expected max:   ${expected_max_mb}MB"
  echo "Flash capacity: ${max_flash_mb}MB"

  SIZE_MB="$size_mb" EXPECTED_MAX_MB="$expected_max_mb" MAX_FLASH_MB="$max_flash_mb" python3 - <<'PY'
import os
import sys

size_mb = float(os.environ["SIZE_MB"])
expected_max_mb = float(os.environ["EXPECTED_MAX_MB"])
flash_max_mb = float(os.environ["MAX_FLASH_MB"])

if size_mb > flash_max_mb:
    print(f"Firmware size {size_mb:.2f}MB exceeds flash capacity {flash_max_mb:.2f}MB", file=sys.stderr)
    sys.exit(1)
if size_mb > expected_max_mb:
    print(f"Warning: firmware size {size_mb:.2f}MB exceeds expected {expected_max_mb:.2f}MB")
else:
    print(f"Size check passed: {size_mb:.2f}MB")
PY
}

run_custom_profile_build() {
  local profile="$1"
  local build_dir="$2"
  local suffix="$3"

  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  run_py "$PY_BUILD" python scripts/generate_build_config.py --profile "$profile"
  if [[ -n "$suffix" ]]; then
    sed -i "s/-custom\\\\\"/${suffix}\\\\\"/g" platformio-custom.ini
  fi
  PLATFORMIO_BUILD_DIR="$build_dir" run_py "$PY_BUILD" pio run -e custom | tee "${build_dir}.log"
  run_py "$PY_BUILD" python scripts/check_flash_headroom.py \
    --log "${build_dir}.log" \
    --firmware "${build_dir}/custom/firmware.bin" \
    --min-headroom-kb 64
}

run_profile_matrix_entry() {
  local matrix_id="$1"
  local profile="$2"
  local expected_max_mb="$3"
  shift 3

  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  run_py "$PY_BUILD" python scripts/generate_build_config.py --profile "$profile" "$@"
  PLATFORMIO_BUILD_DIR=".pio-${matrix_id}" run_py "$PY_BUILD" pio run -e custom
  validate_firmware_size ".pio-${matrix_id}/custom/firmware.bin" "$expected_max_mb" "6.4"
}

run_ci_build() {
  run_clang_format_check
  run_feature_sync_check
  run_host_tests
  run_contract_server_check
  run_hardware_pattern_check
  run_cppcheck
}

run_build_workflow() {
  sync_env "$PY_BUILD"
  ensure_pio_python "$PY_BUILD"
  run_py "$PY_BUILD" python scripts/check_feature_key_sync.py
  run_py "$PY_BUILD" pio check -e default
  bash test/run_host_tests.sh

  run_custom_profile_build lean ".pio-lean-ci" "-lean"
  run_custom_profile_build standard ".pio-standard-ci" ""
  run_custom_profile_build full ".pio-full-ci" "-full"

  run_py "$PY_BUILD" pio run -e gh_latest | tee .pio-gh-latest.log
  run_py "$PY_BUILD" python scripts/check_flash_headroom.py \
    --log .pio-gh-latest.log \
    --firmware .pio/build/gh_latest/firmware.bin \
    --min-headroom-kb 64
}

run_profile_matrix() {
  run_profile_matrix_entry lean lean 5.7
  run_profile_matrix_entry standard standard 6.1
  run_profile_matrix_entry full full 6.6
  run_profile_matrix_entry full_no_md full 6.1 --disable markdown
  run_profile_matrix_entry lean_md lean 6.2 --enable markdown
  run_profile_matrix_entry standard_sync standard 6.2 --enable koreader_sync --enable calibre_sync
}

run_update_screen_previews() {
  ensure_python "$PY_BUILD"
  tools/screen-harness/render_screens.sh build/screen-previews
  uv run --python "$PY_BUILD" --with pillow python tools/screen-harness/pbm_to_png.py \
    build/screen-previews docs/configurator/screen-previews
  python3 - <<'PY'
import json
from datetime import datetime, timezone
from pathlib import Path

out_dir = Path("docs/configurator/screen-previews")
out_dir.mkdir(parents=True, exist_ok=True)

labels = {
    "01_boot": "Boot",
    "02_home_mock": "Home",
    "03_settings_mock": "Settings",
    "04_factory_reset_mock": "Factory Reset",
    "05_reader_mock": "Reader",
    "06_feature_store_mock": "Feature Store",
}

for png in out_dir.glob("*.png"):
    if png.stem not in labels:
        png.unlink()

files = []
for stem, label in labels.items():
    png = out_dir / f"{stem}.png"
    if not png.exists():
        continue
    stem = png.stem
    files.append({
        "name": png.name,
        "label": label,
    })

manifest = {
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "files": files,
}

(out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
PY
}

usage() {
  cat <<'EOF'
Usage: scripts/ci/run-local.sh <target>

Targets:
  ci-build               Run the local equivalent of .github/workflows/ci.yml checks.
  build-workflow         Run the local equivalent of .github/workflows/build.yml.
  profile-matrix         Run the local equivalent of .github/workflows/feature-matrix-test.yml.
  update-screen-previews Run the local equivalent of .github/workflows/update-screen-previews.yml.
  all                    Run all local CI entrypoints above.
EOF
}

target="${1:-}"

case "$target" in
  ci-build)
    run_ci_build
    ;;
  build-workflow)
    run_build_workflow
    ;;
  profile-matrix)
    run_profile_matrix
    ;;
  update-screen-previews)
    run_update_screen_previews
    ;;
  all)
    run_ci_build
    run_build_workflow
    run_profile_matrix
    run_update_screen_previews
    ;;
  *)
    usage
    exit 1
    ;;
esac

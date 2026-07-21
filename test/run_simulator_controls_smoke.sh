#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
declare -a SMOKE_LOGS=()

cleanup() {
  if ((${#SMOKE_LOGS[@]} > 0)); then
    rm -f -- "${SMOKE_LOGS[@]}"
  fi
}
trap cleanup EXIT

run_case() {
  local name="$1"
  local total_budget="$2"
  local largest_cap="$3"
  local expected_marker="$4"
  local expect_recovery="$5"
  local expect_preview_retry="$6"
  local log_file
  log_file="$(mktemp "${TMPDIR:-/tmp}/forkdrift-controls-smoke.XXXXXX.log")"
  SMOKE_LOGS+=("$log_file")
  local -a env_args=("SIM_HEAP_BUDGET=$total_budget")

  if [[ -n "$largest_cap" ]]; then
    env_args+=("SIM_HEAP_LARGEST=$largest_cap")
  fi
  if [[ "$expect_recovery" == "yes" ]]; then
    env_args+=("FORKDRIFT_SIMULATOR_SMOKE_CONTROLS_EXPECT_RECOVERY=1")
  fi

  echo "Testing $name (total=$total_budget largest=${largest_cap:-current-free})..."
  if ! env "${env_args[@]}" uv run python scripts/run_simulator_smoke_test.py \
    --controls-options --no-build >"$log_file" 2>&1; then
    cat "$log_file" >&2
    return 1
  fi

  if grep -Fq "SIM OOM" "$log_file"; then
    echo "$name reached simulator OOM instead of the requested Controls state" >&2
    cat "$log_file" >&2
    return 1
  fi

  local marker_count
  marker_count="$(grep -Fc "$expected_marker" "$log_file" || true)"
  if [[ "$marker_count" -ne 1 ]]; then
    echo "$name expected exactly one '$expected_marker' marker, found $marker_count" >&2
    cat "$log_file" >&2
    return 1
  fi

  local retry_count
  retry_count="$(grep -Fc "SMOKE_CTRL_RETRY_WITHOUT_PREVIEW" "$log_file" || true)"
  if [[ "$expect_preview_retry" == "yes" && "$retry_count" -ne 1 ]]; then
    echo "$name expected exactly one preview-release retry, found $retry_count" >&2
    cat "$log_file" >&2
    return 1
  fi
  if [[ "$expect_preview_retry" == "no" && "$retry_count" -ne 0 ]]; then
    echo "$name unexpectedly retried after releasing its preview" >&2
    cat "$log_file" >&2
    return 1
  fi

  local propagation_count
  propagation_count="$(grep -Fc "SMOKE_READER_MEMORY_RECOVERY_PROPAGATED" "$log_file" || true)"
  if [[ "$expect_recovery" == "yes" && "$propagation_count" -ne 1 ]]; then
    echo "$name expected exactly one reader-side recovery propagation, found $propagation_count" >&2
    cat "$log_file" >&2
    return 1
  fi
  if [[ "$expect_recovery" == "no" && "$propagation_count" -ne 0 ]]; then
    echo "$name unexpectedly propagated recovery to Reader" >&2
    cat "$log_file" >&2
    return 1
  fi

  if ! grep -Fq "Simulator smoke test passed" "$log_file"; then
    echo "$name did not complete the simulator smoke" >&2
    cat "$log_file" >&2
    return 1
  fi

  grep -E "Pre-menu heap|Pre-build ControlsOptions heap|SMOKE_CTRL_|SMOKE_READER_MEMORY_RECOVERY_PROPAGATED|Simulator smoke test passed" \
    "$log_file"
}

cd "$ROOT_DIR"

echo "Building simulator..."
uv run pio run -e simulator

run_case "healthy retained preview" 300000 "" \
  "SMOKE_CTRL_READY preview=retained selectable=1" no no
run_case "preview-release retry" 160720 "" \
  "SMOKE_CTRL_READY preview=absent selectable=1" no yes
run_case "healthy dropped preview" 130000 "" \
  "SMOKE_CTRL_READY preview=absent selectable=1" no no
run_case "low-total typed recovery" 95000 "" \
  "SMOKE_CTRL_TYPED_RECOVERY" yes no
run_case "fragmented-largest typed recovery" 300000 47000 \
  "SMOKE_CTRL_TYPED_RECOVERY" yes no

echo "All controls smoke assertions passed"

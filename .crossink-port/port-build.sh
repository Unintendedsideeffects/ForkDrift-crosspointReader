#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOCK_FILE="${SCRIPT_DIR}/.build.lock"
LAST_BUILD="${SCRIPT_DIR}/.last-build"
LOCK_WAIT_SECS="${PORT_BUILD_LOCK_WAIT_SECS:-7200}"
BUILD_LOG="${SCRIPT_DIR}/.build-last.log"

die() {
  echo "port-build: $*" >&2
  exit 3
}

fingerprint_line() {
  local head work staged
  head="$(git -C "${REPO_ROOT}" rev-parse HEAD 2>/dev/null)" || die "not a git repository"
  work="$(git -C "${REPO_ROOT}" diff | sha256sum | awk '{print substr($1,1,16)}')"
  staged="$(git -C "${REPO_ROOT}" diff --cached | sha256sum | awk '{print substr($1,1,16)}')"
  echo "${head}:${work}:${staged}"
}

read_last_field() {
  local key="$1"
  [[ -f "${LAST_BUILD}" ]] || return 1
  grep -E "^${key}=" "${LAST_BUILD}" 2>/dev/null | tail -1 | cut -d= -f2-
}

write_last_build() {
  local status="$1" fingerprint="$2" ram="${3:-}"
  local head short_head
  head="$(git -C "${REPO_ROOT}" rev-parse HEAD)"
  short_head="$(git -C "${REPO_ROOT}" rev-parse --short HEAD)"
  {
    echo "status=${status}"
    echo "head=${head}"
    echo "short_head=${short_head}"
    echo "fingerprint=${fingerprint}"
    echo "ram_percent=${ram}"
    echo "timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } >"${LAST_BUILD}"
}

cache_hit_if_current() {
  local fp="$1"
  if [[ "$(read_last_field status || true)" == "ok" && "$(read_last_field fingerprint || true)" == "${fp}" ]]; then
    echo "BUILD_OK_CACHED (same tree as $(read_last_field short_head))"
    exit 0
  fi
}

main() {
  local fp
  fp="$(fingerprint_line)"
  cache_hit_if_current "${fp}"

  exec 9>"${LOCK_FILE}"
  if ! flock -w "${LOCK_WAIT_SECS}" 9; then
    echo "BUILD_BUSY_RETRY" >&2
    exit 2
  fi

  cache_hit_if_current "${fp}"

  command -v uv >/dev/null 2>&1 || die "uv not found"
  set +e
  (cd "${REPO_ROOT}" && uv run pio run) 2>&1 | tee "${BUILD_LOG}"
  local ec=${PIPESTATUS[0]}
  set -e

  if [[ "${ec}" -eq 0 ]]; then
    local ram
    ram="$(grep -E 'RAM:.*[0-9]+\.[0-9]+%' "${BUILD_LOG}" | tail -1 | grep -oE '[0-9]+\.[0-9]+' | head -1 || true)"
    write_last_build ok "${fp}" "${ram}"
    echo "BUILD_OK${ram:+ ram=${ram}%}"
    exit 0
  fi
  write_last_build fail "${fp}" ""
  echo "BUILD_FAILED" >&2
  exit 1
}

main "$@"

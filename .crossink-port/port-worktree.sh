#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
WORKTREES_ROOT="/home/malcolm/Code/ForkDrift/worktrees"
INTEGRATION_BRANCH="${PORT_INTEGRATION_BRANCH:-feature/absorb-crossink}"

VALID_LANES=(home reader file web settings renderer integration)

die() {
  echo "port-worktree: $*" >&2
  exit 1
}

lane_valid() {
  local lane="$1"
  local x
  for x in "${VALID_LANES[@]}"; do
    [[ "${x}" == "${lane}" ]] && return 0
  done
  return 1
}

worktree_path() {
  echo "${WORKTREES_ROOT}/crosspoint-reader-${1}"
}

branch_name() {
  echo "port/${1}"
}

cmd_create() {
  local lane="${1:-}"
  [[ -n "${lane}" ]] || die "usage: port-worktree.sh create <lane>"
  lane_valid "${lane}" || die "unknown lane '${lane}'; valid: ${VALID_LANES[*]}"

  local wt branch
  wt="$(worktree_path "${lane}")"
  branch="$(branch_name "${lane}")"

  mkdir -p "${WORKTREES_ROOT}"

  if [[ -d "${wt}" ]]; then
    die "worktree already exists: ${wt} (use 'remove ${lane}' first)"
  fi

  git -C "${REPO_ROOT}" fetch origin "${INTEGRATION_BRANCH}" 2>/dev/null || true

  if [[ "${lane}" == "integration" ]]; then
    git -C "${REPO_ROOT}" worktree add "${wt}" "${INTEGRATION_BRANCH}"
    echo "Created integration worktree: ${wt}"
    echo "Branch: ${INTEGRATION_BRANCH} (merge port/<lane> here one at a time)"
    echo "Next: cd ${wt} && git merge --no-ff origin/port/<lane> && ./.crossink-port/port-build.sh"
    return 0
  fi

  git -C "${REPO_ROOT}" worktree add -b "${branch}" "${wt}" "${INTEGRATION_BRANCH}"

  echo "Created worktree: ${wt}"
  echo "Branch: ${branch} (from ${INTEGRATION_BRANCH})"
  echo "Next: cd ${wt} && implement → ./.crossink-port/port-build.sh → commit → push -u origin ${branch}"
}

cmd_list() {
  echo "Integration: ${REPO_ROOT} ($(git -C "${REPO_ROOT}" branch --show-current 2>/dev/null || echo '?'))"
  echo
  git -C "${REPO_ROOT}" worktree list
}

cmd_remove() {
  local lane="${1:-}"
  [[ -n "${lane}" ]] || die "usage: port-worktree.sh remove <lane>"
  lane_valid "${lane}" || die "unknown lane '${lane}'; valid: ${VALID_LANES[*]}"

  local wt branch
  wt="$(worktree_path "${lane}")"
  branch="$(branch_name "${lane}")"

  [[ -d "${wt}" ]] || die "no worktree at ${wt}"

  git -C "${REPO_ROOT}" worktree remove "${wt}" --force
  echo "Removed worktree: ${wt}"

  if [[ "${PORT_DELETE_BRANCH:-}" == 1 ]] && git -C "${REPO_ROOT}" show-ref --verify --quiet "refs/heads/${branch}"; then
    git -C "${REPO_ROOT}" branch -D "${branch}"
    echo "Deleted branch ${branch}"
  else
    echo "Branch ${branch} kept (set PORT_DELETE_BRANCH=1 to delete)"
  fi
}

usage() {
  cat <<EOF
Usage: port-worktree.sh <command> [lane]

Commands:
  create <lane>   Add worktree at ${WORKTREES_ROOT}/crosspoint-reader-<lane>
                  on new branch port/<lane> from ${INTEGRATION_BRANCH}
  list            Show integration path and all git worktrees
  remove <lane>   Remove lane worktree (optional branch delete prompt)

Lanes: ${VALID_LANES[*]}
EOF
}

main() {
  local cmd="${1:-}"
  shift || true
  case "${cmd}" in
    create) cmd_create "${1:-}" ;;
    list) cmd_list ;;
    remove) cmd_remove "${1:-}" ;;
    -h|--help|help|"") usage ;;
    *) die "unknown command '${cmd}'" ;;
  esac
}

main "$@"

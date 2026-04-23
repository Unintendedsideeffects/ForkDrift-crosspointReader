#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

uv python install 3.11 3.14
uv sync --frozen --python 3.11
uv sync --frozen --python 3.14
uv run --python 3.11 python -m ensurepip --upgrade || true

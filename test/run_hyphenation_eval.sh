#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/test"
BINARY="$BUILD_DIR/hyphenation_eval/HyphenationEvaluationTest"

cmake -S "$ROOT_DIR/test" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target HyphenationEvaluationTest --parallel

"$BINARY" "$@"

#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGRAPHX_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" -j "${GRAPHX_BUILD_JOBS:-4}"
ctest --test-dir "$BUILD_DIR" --output-on-failure -L quick
echo "Configuration, transport, ownership and compiled execution contracts passed"

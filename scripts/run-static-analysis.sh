#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${GRAPHX_QUALITY_BUILD_DIR:-"$ROOT/build/quality"}
CLANG_TIDY_BIN=${CLANG_TIDY:-clang-tidy-18}
CPPCHECK_BIN=${CPPCHECK:-cppcheck}
REQUIRED_CLANG_TIDY_MAJOR=18

for tool in cmake ninja "$CLANG_TIDY_BIN" "$CPPCHECK_BIN"; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

CLANG_TIDY_VERSION=$($CLANG_TIDY_BIN --version)
case "$CLANG_TIDY_VERSION" in
  *"version $REQUIRED_CLANG_TIDY_MAJOR."*) ;;
  *)
    echo "GraphX static analysis requires clang-tidy $REQUIRED_CLANG_TIDY_MAJOR.x; found: $CLANG_TIDY_VERSION" >&2
    exit 2
    ;;
esac

cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGRAPHX_BUILD_TESTS=ON \
  -DGRAPHX_ENABLE_CLANG_TIDY=ON \
  -DGRAPHX_ENABLE_CPPCHECK=ON \
  -DGRAPHX_CLANG_TIDY_EXECUTABLE="$(command -v "$CLANG_TIDY_BIN")" \
  -DGRAPHX_CPPCHECK_EXECUTABLE="$(command -v "$CPPCHECK_BIN")"
cmake --build "$BUILD_DIR" -j "${GRAPHX_BUILD_JOBS:-4}"

echo "clang-tidy and cppcheck passed for GraphX production, application, and test targets"

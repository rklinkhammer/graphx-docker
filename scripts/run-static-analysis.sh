#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${GRAPHX_QUALITY_BUILD_DIR:-"$ROOT/build/quality"}
CLANG_TIDY_BIN=${CLANG_TIDY:-clang-tidy-21}
CPPCHECK_BIN=${CPPCHECK:-cppcheck}
REQUIRED_CLANG_TIDY_MAJOR=21

if test "$(uname -s)" = Darwin; then
  command -v xcrun >/dev/null || {
    echo "missing prerequisite: xcrun from Xcode Command Line Tools" >&2
    exit 2
  }
  SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path)}
  test -d "$SDKROOT" || { echo "active macOS SDK not found: $SDKROOT" >&2; exit 2; }
  export SDKROOT
fi

for tool in cmake ninja "$CLANG_TIDY_BIN" "$CPPCHECK_BIN"; do
  command -v "$tool" >/dev/null || {
    echo "missing prerequisite: $tool" >&2
    if test "$tool" = "$CLANG_TIDY_BIN"; then
      echo "Set CLANG_TIDY to the LLVM 21 analyzer path; see docs/test-procedure.md#macos-llvm-21-setup" >&2
    fi
    exit 2
  }
done

CLANG_TIDY_VERSION=$($CLANG_TIDY_BIN --version)
case "$CLANG_TIDY_VERSION" in
  *"version $REQUIRED_CLANG_TIDY_MAJOR."*) ;;
  *)
    echo "GraphX static analysis requires clang-tidy $REQUIRED_CLANG_TIDY_MAJOR.x; found: $CLANG_TIDY_VERSION" >&2
    exit 2
    ;;
esac

cmake --fresh -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
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

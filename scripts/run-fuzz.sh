#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/lib/llvm21-toolchain.sh"
BUILD_DIR=${GRAPHX_FUZZ_BUILD_DIR:-"$ROOT/build/fuzz"}
RUN_SECONDS=${GRAPHX_FUZZ_SECONDS:-15}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-fuzz.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

for tool in cmake ninja xxd; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

select_llvm21_toolchain

mkdir -p "$TMP_DIR/envelope" "$TMP_DIR/frame"
xxd -r -p "$ROOT/tests/fixtures/envelope.hex" >"$TMP_DIR/envelope/current"
cp "$TMP_DIR/envelope/current" "$TMP_DIR/frame/current-envelope"

cmake --fresh -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DGRAPHX_BUILD_TESTS=OFF \
  -DGRAPHX_ENABLE_SANITIZERS=ON \
  -DGRAPHX_SANITIZERS="${GRAPHX_SANITIZERS:-address,undefined}" \
  -DGRAPHX_BUILD_FUZZERS=ON
cmake --build "$BUILD_DIR" --target graphx-envelope-fuzz graphx-frame-fuzz \
  -j "${GRAPHX_BUILD_JOBS:-4}"
cmake \
  -DGRAPHX_COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json" \
  -DGRAPHX_SOURCE_ROOT="$ROOT" \
  -DGRAPHX_EXPECTED_SANITIZERS="${GRAPHX_SANITIZERS:-address,undefined}" \
  -DGRAPHX_REQUIRE_TEST_SOURCES=OFF \
  -DGRAPHX_REQUIRE_FUZZ_SOURCES=ON \
  -P "$ROOT/cmake/verify-sanitizer-coverage.cmake"

"$BUILD_DIR/graphx-envelope-fuzz" "$TMP_DIR/envelope" \
  -dict="$ROOT/fuzz/envelope.dict" -max_len=1048576 -timeout=5 \
  -max_total_time="$RUN_SECONDS" -print_final_stats=1
"$BUILD_DIR/graphx-frame-fuzz" "$TMP_DIR/frame" \
  -dict="$ROOT/fuzz/envelope.dict" -max_len=1048576 -timeout=5 \
  -max_total_time="$RUN_SECONDS" -print_final_stats=1

echo "envelope and frame fuzz smoke runs passed (${RUN_SECONDS}s each)"

#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${GRAPHX_FUZZ_BUILD_DIR:-"$ROOT/build/fuzz"}
RUN_SECONDS=${GRAPHX_FUZZ_SECONDS:-15}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-fuzz.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

for tool in cmake ninja xxd; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

if test -z "${CC:-}"; then
  for candidate in clang-18 clang; do
    if command -v "$candidate" >/dev/null; then
      CC=$candidate
      break
    fi
  done
fi
if test -z "${CXX:-}"; then
  for candidate in clang++-18 clang++; do
    if command -v "$candidate" >/dev/null; then
      CXX=$candidate
      break
    fi
  done
fi
if test -z "${CC:-}" || test -z "${CXX:-}"; then
  echo "missing prerequisite: Clang with libFuzzer support (set CC and CXX)" >&2
  exit 2
fi
command -v "$CC" >/dev/null || { echo "compiler not found: CC=$CC" >&2; exit 2; }
command -v "$CXX" >/dev/null || { echo "compiler not found: CXX=$CXX" >&2; exit 2; }
compiler_version=$("$CXX" --version)
if ! grep -qi clang <<<"$compiler_version"; then
  echo "GraphX fuzzing requires Clang; selected CXX=$CXX" >&2
  echo "unset CC/CXX to auto-detect Clang, or set CC=clang CXX=clang++" >&2
  exit 2
fi
export CC CXX
echo "Fuzz compiler: CC=$CC CXX=$CXX"

mkdir -p "$TMP_DIR/envelope" "$TMP_DIR/frame"
xxd -r -p "$ROOT/tests/fixtures/envelope-v1.hex" >"$TMP_DIR/envelope/v1"
xxd -r -p "$ROOT/tests/fixtures/envelope-v2.hex" >"$TMP_DIR/envelope/v2"
cp "$TMP_DIR/envelope/v1" "$TMP_DIR/frame/v1-envelope"
cp "$TMP_DIR/envelope/v2" "$TMP_DIR/frame/v2-envelope"

cmake --fresh -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_STANDARD=23 \
  -DGRAPHX_BUILD_TESTS=OFF \
  -DGRAPHX_ENABLE_SANITIZERS=ON \
  -DGRAPHX_BUILD_FUZZERS=ON
cmake --build "$BUILD_DIR" --target graphx-envelope-fuzz graphx-frame-fuzz \
  -j "${GRAPHX_BUILD_JOBS:-4}"
cmake \
  -DGRAPHX_COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json" \
  -DGRAPHX_SOURCE_ROOT="$ROOT" \
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

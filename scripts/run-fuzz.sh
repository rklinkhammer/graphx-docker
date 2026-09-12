#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${GRAPHX_FUZZ_BUILD_DIR:-"$ROOT/build/fuzz"}
RUN_SECONDS=${GRAPHX_FUZZ_SECONDS:-15}
REQUIRED_CLANG_MAJOR=21
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-fuzz.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

if test "$(uname -s)" = Darwin; then
  command -v xcrun >/dev/null || {
    echo "missing prerequisite: xcrun from Xcode Command Line Tools" >&2
    exit 2
  }
  SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path)}
  test -d "$SDKROOT" || { echo "active macOS SDK not found: $SDKROOT" >&2; exit 2; }
  export SDKROOT
  macos_major=$(sw_vers -productVersion | cut -d. -f1)
  if test "$macos_major" -ge 26 && test -z "${GRAPHX_SANITIZERS:-}"; then
    GRAPHX_SANITIZERS=undefined
    export GRAPHX_SANITIZERS
    echo "NOTE: macOS $macos_major uses LLVM 21 UBSan without ASan because the"
    echo "Homebrew LLVM ASan runtime hangs during process initialization on macOS 26."
    echo "Full LLVM 21 ASan+UBSan acceptance remains enabled on Linux."
  fi
fi

if test -n "${GRAPHX_FUZZ_CC:-}"; then CC=$GRAPHX_FUZZ_CC; fi
if test -n "${GRAPHX_FUZZ_CXX:-}"; then CXX=$GRAPHX_FUZZ_CXX; fi

for tool in cmake ninja xxd; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

if test -z "${CC:-}" && test "$(uname -s)" = Darwin && command -v brew >/dev/null; then
  llvm21_bin=$(brew --prefix llvm@21)/bin
  test -x "$llvm21_bin/clang" && CC=$llvm21_bin/clang
fi
if test -z "${CC:-}"; then
  for candidate in clang-21 clang; do
    if command -v "$candidate" >/dev/null; then
      CC=$candidate
      break
    fi
  done
fi
if test -z "${CXX:-}" && test "$(uname -s)" = Darwin && command -v brew >/dev/null; then
  llvm21_bin=${llvm21_bin:-$(brew --prefix llvm@21)/bin}
  test -x "$llvm21_bin/clang++" && CXX=$llvm21_bin/clang++
fi
if test -z "${CXX:-}"; then
  for candidate in clang++-21 clang++; do
    if command -v "$candidate" >/dev/null; then
      CXX=$candidate
      break
    fi
  done
fi
if test -z "${CC:-}" || test -z "${CXX:-}"; then
  echo "missing prerequisite: Clang with libFuzzer support (set GRAPHX_FUZZ_CC and GRAPHX_FUZZ_CXX)" >&2
  exit 2
fi
command -v "$CC" >/dev/null || { echo "compiler not found: CC=$CC" >&2; exit 2; }
command -v "$CXX" >/dev/null || { echo "compiler not found: CXX=$CXX" >&2; exit 2; }
compiler_version=$("$CXX" --version)
if grep -q "Apple clang" <<<"$compiler_version" || \
    ! grep -q "version $REQUIRED_CLANG_MAJOR\." <<<"$compiler_version"; then
  echo "GraphX fuzzing requires Clang $REQUIRED_CLANG_MAJOR.x; selected CXX=$CXX" >&2
  echo "$compiler_version" >&2
  echo "set GRAPHX_FUZZ_CC and GRAPHX_FUZZ_CXX to the LLVM 21 compiler paths" >&2
  exit 2
fi
export CC CXX
echo "Fuzz compiler: CC=$CC CXX=$CXX"

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

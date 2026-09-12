#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=/workspace
MODE=${1:-tls}
CLANG_GCC_TOOLCHAIN=--gcc-toolchain=/usr/local
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
EVIDENCE_DIR=${GRAPHX_EVIDENCE_DIR:-/evidence}
mkdir -p "$EVIDENCE_DIR"
LOG="$EVIDENCE_DIR/${MODE}-${RUN_ID}.log"
exec > >(tee "$LOG") 2>&1

cd "$ROOT"

echo "GraphX Linux verifier"
echo "mode=$MODE"
echo "run_id=$RUN_ID"
echo "architecture=$(uname -m)"
echo "kernel=$(uname -sr)"
echo "openssl=$(openssl version)"
echo "cmake=$(cmake --version | head -n 1)"
echo "compiler=$(g++ --version | head -n 1)"
echo "libstdcxx_toolchain=$(g++ -dumpfullversion)"
echo "node=$(node --version)"
echo "npm=$(npm --version)"

case "$MODE" in
  tls)
    build=/tmp/graphx-linux-tls
    cmake -S "$ROOT" -B "$build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_STANDARD=23 \
      -DGRAPHX_BUILD_TESTS=ON
    cmake --build "$build" --target graphx-tls-smoke -j "$GRAPHX_BUILD_JOBS"
    ctest --test-dir "$build" -R '^graphx-tls-security$' --output-on-failure --verbose
    ;;
  ctest)
    for standard in 23 20; do
      build="/tmp/graphx-linux-cxx${standard}"
      cmake -S "$ROOT" -B "$build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_STANDARD="$standard" \
        -DGRAPHX_BUILD_TESTS=ON
      cmake --build "$build" -j "$GRAPHX_BUILD_JOBS"
      ctest --test-dir "$build" --output-on-failure
    done
    ;;
  portable)
    GRAPHX_BUILD_DIR=/tmp/graphx-linux-portable-cxx23 \
    GRAPHX_CXX20_BUILD_DIR=/tmp/graphx-linux-portable-cxx20 \
      "$ROOT/scripts/test-features.sh" portable
    ;;
  quality)
    case $(g++ -dumpfullversion) in
      15.*) ;;
      *) echo "Linux quality requires the GCC 15 libstdc++ toolchain" >&2; exit 2 ;;
    esac
    printf '%s\n' '#include <bits/c++config.h>' \
      'static_assert(_GLIBCXX_RELEASE == 15);' \
      | clang++-21 "$CLANG_GCC_TOOLCHAIN" -x c++ -fsyntax-only -
    CLANG_FORMAT=clang-format-21 "$ROOT/scripts/check-format.sh"
    CC=clang-21 CXX=clang++-21 \
    CXXFLAGS="$CLANG_GCC_TOOLCHAIN" \
    CLANG_TIDY=clang-tidy-21 \
    CPPCHECK=cppcheck \
    GRAPHX_QUALITY_BUILD_DIR=/tmp/graphx-linux-quality \
      "$ROOT/scripts/run-static-analysis.sh"
    ;;
  sanitizers)
    build=/tmp/graphx-linux-sanitizers
    CC=clang-21 CXX=clang++-21 cmake -S "$ROOT" -B "$build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_STANDARD=23 \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DGRAPHX_BUILD_TESTS=ON \
      -DGRAPHX_ENABLE_SANITIZERS=ON
    cmake --build "$build" -j "$GRAPHX_BUILD_JOBS"
    ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer-21 \
    ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
    UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
      ctest --test-dir "$build" --output-on-failure
    ;;
  fuzz)
    GRAPHX_FUZZ_BUILD_DIR=/tmp/graphx-linux-fuzz \
    GRAPHX_FUZZ_SECONDS="${GRAPHX_FUZZ_SECONDS:-30}" \
      "$ROOT/scripts/run-fuzz.sh"
    ;;
  shell)
    exec /usr/bin/bash
    ;;
  *)
    echo "usage: $0 {tls|ctest|portable|quality|sanitizers|fuzz|shell}" >&2
    exit 64
    ;;
esac

echo "PASS: GraphX Linux verifier mode '$MODE'"
echo "evidence=$LOG"

#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=/workspace
MODE=${1:-tls}
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
    CLANG_FORMAT=clang-format-18 "$ROOT/scripts/check-format.sh"
    CLANG_TIDY=clang-tidy-18 \
    CPPCHECK=cppcheck \
    GRAPHX_QUALITY_BUILD_DIR=/tmp/graphx-linux-quality \
      "$ROOT/scripts/run-static-analysis.sh"
    CC=clang-18 CXX=clang++-18 \
    GRAPHX_FUZZ_BUILD_DIR=/tmp/graphx-linux-fuzz \
    GRAPHX_FUZZ_SECONDS="${GRAPHX_FUZZ_SECONDS:-30}" \
      "$ROOT/scripts/run-fuzz.sh"
    ;;
  shell)
    exec /usr/bin/bash
    ;;
  *)
    echo "usage: $0 {tls|ctest|portable|quality|shell}" >&2
    exit 64
    ;;
esac

echo "PASS: GraphX Linux verifier mode '$MODE'"
echo "evidence=$LOG"

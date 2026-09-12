#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PROFILE=${1:-help}

usage() {
  cat <<'EOF'
usage: scripts/verify.sh <profile>

Profiles:
  quick         Configure, build, and run the development CTest suite
  quality       Run formatting and static analysis
  sanitizers    Run the platform-safe LLVM 21 sanitizer suite
  fuzz          Run bounded LLVM 21 libFuzzer smoke tests
  portable      Run complete non-Docker acceptance for C++20 and C++23
  full          Run quality, sanitizers, fuzzing, portable, and Docker acceptance
  native-linux  Run portable and privileged native Linux network acceptance
  release       Build and independently verify a clean local release candidate

Detailed requirements and platform notes: docs/test-procedure.md
EOF
}

case "$PROFILE" in
  help|-h|--help)
    usage
    exit 0
    ;;
  quick|quality|sanitizers|fuzz|portable|full|native-linux|release) ;;
  *)
    echo "unknown verification profile: $PROFILE" >&2
    usage >&2
    exit 64
    ;;
esac

test "$#" -eq 1 || {
  usage >&2
  exit 64
}

RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
LOG_DIR=${GRAPHX_VERIFY_LOG_DIR:-"$ROOT/outputs/verification"}
LOG_FILE="$LOG_DIR/${RUN_ID}-${PROFILE}.log"
mkdir -p "$LOG_DIR"

CURRENT_GATE=initialization
START_SECONDS=$SECONDS

finish() {
  status=$?
  elapsed=$((SECONDS - START_SECONDS))
  if test "$status" -eq 0; then
    printf '\nPASS: %s verification completed in %ss\n' "$PROFILE" "$elapsed"
  else
    printf '\nFAIL: %s verification stopped during %s (exit %s)\n' \
      "$PROFILE" "$CURRENT_GATE" "$status"
  fi
  printf 'Log: %s\n' "$LOG_FILE"
}

trap finish EXIT
exec > >(tee -a "$LOG_FILE") 2>&1
cd "$ROOT"

gate() {
  CURRENT_GATE=$1
  printf '\n==> %s\n' "$CURRENT_GATE"
}

run_quick() {
  local dev_build_dir=${GRAPHX_DEV_BUILD_DIR:-}
  gate "configure development build"
  if test -n "$dev_build_dir"; then
    mkdir -p "$dev_build_dir"
    cmake --preset dev --fresh -B "$dev_build_dir"
    gate "build development targets"
    cmake --build "$dev_build_dir" -j "${GRAPHX_BUILD_JOBS:-4}"
    gate "development CTest suite"
    ctest --test-dir "$dev_build_dir" --output-on-failure -L quick
  else
    cmake --preset dev --fresh
    gate "build development targets"
    cmake --build --preset dev -j "${GRAPHX_BUILD_JOBS:-4}"
    gate "development CTest suite"
    ctest --preset dev -L quick
  fi
}

preflight_docker() {
  local context
  command -v docker >/dev/null || {
    echo "full verification requires the Docker CLI" >&2
    return 2
  }
  docker compose version >/dev/null || {
    echo "full verification requires Docker Compose" >&2
    return 2
  }
  docker info >/dev/null || {
    echo "full verification requires a reachable Docker engine; start the selected context first" >&2
    return 2
  }
  if test "$(uname -s)" = Darwin; then
    context=$(docker context show)
    test "$context" = orbstack || {
      echo "GraphX macOS Docker acceptance requires the orbstack context; selected: $context" >&2
      return 2
    }
  fi
}

select_llvm21_sanitizer_toolchain() {
  local compiler_dir compiler_version compatible_sdk macos_major sdk_major sdk_root

  if test -n "${GRAPHX_SANITIZER_CC:-}" || test -n "${GRAPHX_SANITIZER_CXX:-}"; then
    test -n "${GRAPHX_SANITIZER_CC:-}" && test -n "${GRAPHX_SANITIZER_CXX:-}" || {
      echo "set both GRAPHX_SANITIZER_CC and GRAPHX_SANITIZER_CXX" >&2
      return 2
    }
    CC=$GRAPHX_SANITIZER_CC
    CXX=$GRAPHX_SANITIZER_CXX
  elif test "$(uname -s)" = Darwin; then
    command -v brew >/dev/null || {
      echo "LLVM 21 sanitizer testing on macOS requires Homebrew llvm@21" >&2
      return 2
    }
    compiler_dir=$(brew --prefix llvm@21)/bin
    CC=$compiler_dir/clang
    CXX=$compiler_dir/clang++
    CLANG_FORMAT=${CLANG_FORMAT:-$compiler_dir/clang-format}
    CLANG_TIDY=${CLANG_TIDY:-$compiler_dir/clang-tidy}
    export CLANG_FORMAT CLANG_TIDY
    command -v xcrun >/dev/null || {
      echo "LLVM 21 testing on macOS requires Xcode Command Line Tools" >&2
      return 2
    }
    macos_major=$(sw_vers -productVersion | cut -d. -f1)
    sdk_root=$(xcrun --show-sdk-path)
    sdk_major=$(xcrun --show-sdk-version | cut -d. -f1)
    if test "$sdk_major" -gt "$macos_major"; then
      compatible_sdk=$(dirname "$sdk_root")/MacOSX${macos_major}.sdk
      if test -d "$compatible_sdk"; then
        echo "NOTE: LLVM 21 selects installed macOS $macos_major SDK instead of newer SDK $sdk_major."
        sdk_root=$compatible_sdk
      fi
    fi
    test -d "$sdk_root" || {
      echo "active macOS SDK not found: $sdk_root" >&2
      return 2
    }
    SDKROOT=$sdk_root
    export SDKROOT
    if test "$macos_major" -ge 26 && test -z "${GRAPHX_SANITIZERS:-}"; then
      GRAPHX_SANITIZERS=undefined
      export GRAPHX_SANITIZERS
      echo "NOTE: macOS $macos_major uses LLVM 21 UBSan without ASan because the"
      echo "Homebrew LLVM ASan runtime hangs during process initialization on macOS 26."
      echo "Full LLVM 21 ASan+UBSan acceptance remains enabled on Linux."
    fi
  else
    CC=clang-21
    CXX=clang++-21
  fi

  command -v "$CC" >/dev/null || { echo "LLVM 21 compiler not found: $CC" >&2; return 2; }
  command -v "$CXX" >/dev/null || { echo "LLVM 21 compiler not found: $CXX" >&2; return 2; }
  compiler_version=$("$CXX" --version)
  if grep -q "Apple clang" <<<"$compiler_version" || \
      ! grep -q "version 21\." <<<"$compiler_version"; then
    echo "GraphX sanitizer acceptance requires LLVM 21.x; found: $compiler_version" >&2
    return 2
  fi

  compiler_dir=$(dirname "$(command -v "$CXX")")
  if test -x "$compiler_dir/llvm-symbolizer"; then
    ASAN_SYMBOLIZER_PATH=$compiler_dir/llvm-symbolizer
    export ASAN_SYMBOLIZER_PATH
  elif command -v llvm-symbolizer-21 >/dev/null; then
    ASAN_SYMBOLIZER_PATH=$(command -v llvm-symbolizer-21)
    export ASAN_SYMBOLIZER_PATH
  fi
  export CC CXX
  echo "Sanitizer compiler: CC=$CC CXX=$CXX"
  test -z "${SDKROOT:-}" || echo "macOS SDK: SDKROOT=$SDKROOT"
}

run_sanitizers() {
  select_llvm21_sanitizer_toolchain
  gate "configure sanitizer build"
  cmake --preset sanitizers --fresh \
    -DGRAPHX_SANITIZERS="${GRAPHX_SANITIZERS:-address,undefined}"
  gate "build sanitizer targets"
  cmake --build --preset sanitizers -j "${GRAPHX_BUILD_JOBS:-4}"
  gate "${GRAPHX_SANITIZERS:-address,undefined} sanitizer CTest suite"
  if test "$(uname -s)" = Darwin; then
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0} \
      UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1} \
      ctest --preset sanitizers
  else
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} \
      UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1} \
      ctest --preset sanitizers
  fi
}

run_release() {
  local version build_dir output_dir
  version=$(tr -d '\n' < VERSION)
  build_dir="$ROOT/build/release-${RUN_ID}"
  output_dir="$ROOT/outputs/release-${RUN_ID}"

  gate "validate release version"
  python3 scripts/release/validate_version.py --tag "v${version}"
  gate "build clean local release candidate"
  python3 scripts/release/build_release.py \
    --build-dir "$build_dir" \
    --output-dir "$output_dir" \
    --tag "v${version}"
  gate "independently verify release candidate"
  python3 scripts/release/verify_release.py "$output_dir" --source .
  printf 'Release candidate: %s\n' "$output_dir"
}

case "$PROFILE" in
  quick)
    run_quick
    ;;
  quality)
    select_llvm21_sanitizer_toolchain
    gate "format"
    scripts/check-format.sh
    gate "static analysis"
    scripts/run-static-analysis.sh
    ;;
  sanitizers)
    run_sanitizers
    ;;
  fuzz)
    select_llvm21_sanitizer_toolchain
    gate "bounded fuzz smoke tests"
    GRAPHX_FUZZ_SECONDS=${GRAPHX_FUZZ_SECONDS:-30} scripts/run-fuzz.sh
    ;;
  portable)
    gate "portable acceptance"
    scripts/test-features.sh portable
    ;;
  full)
    gate "Docker engine preflight"
    preflight_docker
    select_llvm21_sanitizer_toolchain
    gate "format"
    scripts/check-format.sh
    gate "static analysis"
    scripts/run-static-analysis.sh
    run_sanitizers
    gate "bounded fuzz smoke tests"
    GRAPHX_FUZZ_SECONDS=${GRAPHX_FUZZ_SECONDS:-30} scripts/run-fuzz.sh
    gate "portable and Docker acceptance"
    scripts/test-features.sh docker
    ;;
  native-linux)
    test "$(uname -s)" = Linux || {
      echo "native-linux verification requires a native Linux host" >&2
      exit 2
    }
    test "${GRAPHX_ALLOW_PRIVILEGED_TESTS:-}" = 1 || {
      echo "set GRAPHX_ALLOW_PRIVILEGED_TESTS=1 after reviewing the native Linux section" >&2
      exit 2
    }
    command -v dumpcap >/dev/null || {
      echo "native-linux verification requires dumpcap for the live capture gate" >&2
      exit 2
    }
    command -v tshark >/dev/null || {
      echo "native-linux verification requires tshark for the dissector gate" >&2
      exit 2
    }
    gate "portable and privileged native Linux network acceptance"
    scripts/test-features.sh linux-network
    gate "native Linux SDR OVS/SPAN acceptance"
    trap 'examples/sdr-node/external/scripts/demo.sh stop >/dev/null 2>&1 || true' EXIT
    examples/sdr-node/external/scripts/demo.sh start
    examples/sdr-node/external/scripts/demo.sh verify
    examples/sdr-node/external/scripts/demo.sh stop
    trap - EXIT
    gate "native Linux static-route and deny-policy acceptance"
    route_graphx="${GRAPHX_BUILD_DIR:-$ROOT/build/dev}/graphx"
    trap 'examples/static-route-policy/scripts/demo.sh down >/dev/null 2>&1 || true' EXIT
    for _ in 1 2; do
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh up
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh status
      sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.2.10
      ! sudo ip netns exec gx-route-middle-end ping -c 1 -W 1 10.64.1.10
      ! sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
      sudo ip netns exec gx-route-router nft list chain inet graphx forward
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh apply-route
      sudo ip netns exec gx-route-router ip route show 10.64.30.10/32
      sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh clear-route
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh status
      GRAPHX_BIN="$route_graphx" examples/static-route-policy/scripts/demo.sh down
    done
    trap - EXIT
    ;;
  release)
    run_release
    ;;
esac

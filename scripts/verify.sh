#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/lib/llvm21-toolchain.sh"
PROFILE=${1:-help}

usage() {
  cat <<'EOF'
usage: scripts/verify.sh <profile>

Profiles:
  quick         Configure, build, and run the development CTest suite
  quality       Run formatting and static analysis
  sanitizers    Run the platform-safe LLVM 21 sanitizer suite
  fuzz          Run bounded LLVM 21 libFuzzer smoke tests
  portable      Run complete non-Docker acceptance with C++20
  full          Run native quality, macOS-hosted Linux quality, sanitizers,
                fuzzing, portable acceptance, and Docker acceptance
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

run_sanitizers() {
  select_llvm21_toolchain
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
    select_llvm21_toolchain
    gate "format"
    scripts/check-format.sh
    gate "static analysis"
    scripts/run-static-analysis.sh
    ;;
  sanitizers)
    run_sanitizers
    ;;
  fuzz)
    select_llvm21_toolchain
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
    select_llvm21_toolchain
    gate "format"
    scripts/check-format.sh
    gate "static analysis"
    scripts/run-static-analysis.sh
    if test "$(uname -s)" = Darwin; then
      gate "Linux Clang 21 and libstdc++ 15 quality"
      scripts/test-linux-container.sh quality
    fi
    run_sanitizers
    gate "bounded fuzz smoke tests"
    GRAPHX_FUZZ_SECONDS=${GRAPHX_FUZZ_SECONDS:-30} scripts/run-fuzz.sh
    gate "portable and Docker acceptance"
    scripts/test-features.sh docker
    ;;
  native-linux)
    network_build_dir=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
    test "$(uname -s)" = Linux || {
      echo "native-linux verification requires a native Linux host" >&2
      exit 2
    }
    test "${GRAPHX_ALLOW_PRIVILEGED_TESTS:-}" = 1 || {
      echo "set GRAPHX_ALLOW_PRIVILEGED_TESTS=1 after reviewing the native Linux section" >&2
      exit 2
    }
    for prerequisite in sudo docker ovs-vsctl ip nft tc dumpcap tshark capinfos; do
      command -v "$prerequisite" >/dev/null || {
        echo "native-linux verification requires $prerequisite" >&2
        exit 2
      }
    done
    sudo true
    gate "portable acceptance"
    scripts/test-features.sh portable
    gate "configure privileged native Linux CTests"
    if test "$network_build_dir" = "$ROOT/build/dev"; then
      cmake --preset dev -DGRAPHX_ENABLE_LINUX_OVS_TESTS=ON
    else
      cmake -S "$ROOT" -B "$network_build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DGRAPHX_BUILD_TESTS=ON \
        -DGRAPHX_ENABLE_LINUX_OVS_TESTS=ON
    fi
    cmake --build "$network_build_dir" -j "${GRAPHX_BUILD_JOBS:-4}"
    gate "privileged native Linux CTest suite"
    ctest --test-dir "$network_build_dir" --output-on-failure -L privileged
    ;;
  release)
    run_release
    ;;
esac

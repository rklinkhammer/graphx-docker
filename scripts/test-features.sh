#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
MODE=${1:-portable}

isolate_portable_environment() {
  local name
  local build_jobs=${GRAPHX_BUILD_JOBS:-4}
  local build_dir=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
  local http_port=${GRAPHX_TEST_HTTP_PORT:-18080}
  local udp_port=${GRAPHX_TEST_UDP_PORT:-19000}
  while IFS='=' read -r name _; do
    case "$name" in
      GRAPHX_*) unset "$name" ;;
    esac
  done < <(env)
  export GRAPHX_BUILD_JOBS="$build_jobs"
  export GRAPHX_BUILD_DIR="$build_dir"
  export GRAPHX_TEST_HTTP_PORT="$http_port"
  export GRAPHX_TEST_UDP_PORT="$udp_port"
}

portable() (
  local temporary
  isolate_portable_environment
  temporary=$(mktemp -d "${TMPDIR:-/tmp}/graphx-feature-test.XXXXXX")
  trap 'rm -rf "$temporary"' EXIT INT TERM
  export GRAPHX_FEATURE_TMP_DIR="$temporary"
  "$ROOT/scripts/test-runtime-features.sh"
  "$ROOT/scripts/test-telemetry-features.sh"
)

case "$MODE" in
  portable) portable ;;
  docker) "$ROOT/scripts/test-compose-features.sh" ;;
  linux-network) "$ROOT/scripts/test-linux-network-features.sh" ;;
  *) echo "usage: $0 [portable|docker|linux-network]" >&2; exit 64 ;;
esac

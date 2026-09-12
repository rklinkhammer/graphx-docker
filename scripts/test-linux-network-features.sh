#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/test-feature-common.sh"
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
linux_network() {
  test "$(uname -s)" = Linux || { echo "linux-network mode requires native Linux" >&2; exit 2; }
  test "${GRAPHX_ALLOW_PRIVILEGED_TESTS:-}" = 1 || {
    echo "set GRAPHX_ALLOW_PRIVILEGED_TESTS=1 after reviewing docs/test-procedure.md" >&2; exit 2;
  }
  require docker
  require ip
  require ovs-vsctl
  require nft
  "$ROOT/scripts/test-features.sh" portable
  test -x "$BUILD_DIR/graphx" || {
    echo "GraphX CLI was not built at $BUILD_DIR/graphx" >&2
    exit 2
  }
  export GRAPHX_BUILD_DIR="$BUILD_DIR"
  export GRAPHX_BIN="$BUILD_DIR/graphx"
  step "Run native isolated UDP broadcast lab"
  if command -v dumpcap >/dev/null && command -v tshark >/dev/null; then
    GRAPHX_VERIFY_LIVE_CAPTURE=1 "$ROOT/examples/udp-broadcast/run-native-linux.sh"
  else
    echo "SKIP: live UDP capture requires dumpcap and tshark; running delivery gate only"
    "$ROOT/examples/udp-broadcast/run-native-linux.sh"
  fi
  "$ROOT/examples/udp-broadcast/down-native-linux.sh"
  "$ROOT/examples/udp-broadcast/down-native-linux.sh"
  for example in macvlan ipvlan-l2 ipvlan-l3 mixed-network; do
    step "Run native $example lab"
    "$ROOT/examples/$example/scripts/up.sh"
    "$ROOT/examples/$example/scripts/status.sh"
    "$ROOT/examples/$example/scripts/down.sh"
  done
  step "Privileged Linux network suite passed"
}


linux_network


#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MODE=${1:-portable}
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-feature-test.XXXXXX")
PIDS=()

cleanup() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

step() { printf '\n==> %s\n' "$*"; }
require() { command -v "$1" >/dev/null || { echo "missing prerequisite: $1" >&2; exit 2; }; }
wait_for_exit() {
  local pid=$1 name=$2
  for _ in {1..100}; do
    if ! kill -0 "$pid" 2>/dev/null; then wait "$pid"; return; fi
    sleep 0.05
  done
  echo "$name did not stop cleanly" >&2
  return 1
}

portable() {
  require cmake
  require ctest
  require node
  require npm
  require curl

  step "Configure, build, and run the C++23 suite"
  cmake --preset dev -S "$ROOT"
  cmake --build "$BUILD_DIR" -j "${GRAPHX_BUILD_JOBS:-4}"
  ctest --test-dir "$BUILD_DIR" --output-on-failure

  step "Build and test the supported C++20 configuration"
  cmake -S "$ROOT" -B "$ROOT/build/cxx20-features" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=20 -DGRAPHX_BUILD_TESTS=ON
  cmake --build "$ROOT/build/cxx20-features" -j "${GRAPHX_BUILD_JOBS:-4}"
  ctest --test-dir "$ROOT/build/cxx20-features" --output-on-failure

  step "Validate and inspect every checked-in topology"
  "$BUILD_DIR/graphx" validate "$ROOT/graphx.yaml"
  "$BUILD_DIR/graphx" inspect "$ROOT/graphx.yaml" >"$TMP_DIR/inspect.txt"
  for config in "$ROOT"/examples/*/graphx.yaml; do
    "$BUILD_DIR/graphx" validate "$config"
    "$BUILD_DIR/graphx" infra create "$config" --dry-run >"$TMP_DIR/$(basename "$(dirname "$config")").plan"
    "$BUILD_DIR/graphx" infra status "$config" --dry-run >/dev/null
    "$BUILD_DIR/graphx" infra destroy "$config" --dry-run >/dev/null
  done
  "$BUILD_DIR/graphx" infra fault apply "$ROOT/examples/mixed-network/graphx.yaml" \
    --router domain-router --interface mac --delay 20ms --jitter 3ms --loss 1% --dry-run >/dev/null

  step "Run the finite local TCP pipeline"
  export GRAPHX_CONFIG="$ROOT/graphx.yaml"
  export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
  export GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5
  "$BUILD_DIR/graphx-sink" >"$TMP_DIR/sink.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-transform" >"$TMP_DIR/transform.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-generator" >"$TMP_DIR/generator.log" 2>&1
  wait_for_exit "${PIDS[1]}" transform
  wait_for_exit "${PIDS[0]}" sink
  PIDS=()
  grep -q 'sink seq=8 value=16' "$TMP_DIR/sink.log"
  grep -q 'event=connection state=connected' "$TMP_DIR/transform.log"

  step "Verify coordinated SIGTERM shutdown"
  export GRAPHX_MAX_MESSAGES=0 GRAPHX_INTERVAL_MS=10
  "$BUILD_DIR/graphx-sink" >"$TMP_DIR/signal-sink.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-transform" >"$TMP_DIR/signal-transform.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-generator" >"$TMP_DIR/signal-generator.log" 2>&1 & PIDS+=("$!")
  sleep 0.5
  kill -TERM "${PIDS[@]}"
  wait_for_exit "${PIDS[2]}" generator
  wait_for_exit "${PIDS[1]}" transform
  wait_for_exit "${PIDS[0]}" sink
  PIDS=()

  step "Run the finite shared-memory process pipeline"
  unset GRAPHX_OVERRIDES
  GRAPHX_BUILD_DIR="$BUILD_DIR" GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5 \
    "$ROOT/examples/shared-memory/run.sh" >"$TMP_DIR/shared.log"
  grep -q 'sink seq=8 value=16' "$TMP_DIR/shared.log"

  step "Build the web console and exercise telemetry HTTP semantics"
  npm ci --prefix "$ROOT/apps/telemetry" --no-audit --no-fund
  npm ci --prefix "$ROOT/web" --no-audit --no-fund
  npm run build --prefix "$ROOT/web"
  PORT=${GRAPHX_TEST_HTTP_PORT:-18080} GRAPHX_TELEMETRY_PORT=${GRAPHX_TEST_UDP_PORT:-19000} \
    GRAPHX_HEARTBEAT_TIMEOUT_MS=400 GRAPHX_CONFIG="$ROOT/examples/ipvlan-l2/graphx.yaml" \
    GRAPHX_WEB_ROOT="$ROOT/web/dist" node "$ROOT/apps/telemetry/server.mjs" \
    >"$TMP_DIR/telemetry.log" 2>&1 &
  PIDS+=("$!")
  for _ in {1..40}; do curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/health" >/dev/null 2>&1 && break; sleep 0.1; done
  GRAPHX_TEST_UDP_PORT=${GRAPHX_TEST_UDP_PORT:-19000} node -e '
    const d = require("node:dgram").createSocket("udp4");
    const base = {kind:"trace", nodeId:"generator", edgeId:"samples", timestamp:Date.now()};
    const events = [
      {...base,event:"connection",message:"connected"},
      {...base,event:"send",sequence:1,wireBytes:64},
      {...base,event:"receive",sequence:1,wireBytes:64,latencyUs:25},
      {...base,event:"reconnect"},
      {...base,event:"backpressure",latencyUs:40,message:"blocked"}
    ];
    for (const event of events) d.send(JSON.stringify(event), Number(process.env.GRAPHX_TEST_UDP_PORT), "127.0.0.1");
    setTimeout(() => d.close(), 50);
  '
  sleep 0.1
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q 'ipvlan-l2-pipeline'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q '"reconnects":1'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q '"networkNodes"'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q 'br-l2-gen'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/metrics" | grep -q 'graphx_edge_messages_total'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/metrics" | grep -q 'graphx_edge_backpressure_events_total{edge="samples"} 1'
  curl -fsS -X POST "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/reset" | grep -q '"accepted":true'
  test "$(curl -sS -o "$TMP_DIR/pause.json" -w '%{http_code}' -X POST "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/pause")" = 501
  grep -q '"accepted":false' "$TMP_DIR/pause.json"
  sleep 0.6
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q '"status":"offline"'

  step "Portable feature suite passed"
}

docker_suite() {
  require docker
  portable
  step "Validate and smoke-test the standard Compose deployment"
  docker compose -f "$ROOT/compose.yaml" config >/dev/null
  docker compose -f "$ROOT/compose.yaml" up -d --build
  trap 'docker compose -f "$ROOT/compose.yaml" down --remove-orphans; cleanup' EXIT INT TERM
  for _ in {1..60}; do curl -fsS http://127.0.0.1:8080/api/health >/dev/null && break; sleep 1; done
  curl -fsS http://127.0.0.1:8080/api/topology | grep -q 'sample-pipeline'
  docker compose -f "$ROOT/compose.yaml" ps
  docker compose -f "$ROOT/compose.yaml" down --remove-orphans
  trap cleanup EXIT INT TERM
  step "Docker feature suite passed"
}

linux_network() {
  test "$(uname -s)" = Linux || { echo "linux-network mode requires native Linux" >&2; exit 2; }
  test "${GRAPHX_ALLOW_PRIVILEGED_TESTS:-}" = 1 || {
    echo "set GRAPHX_ALLOW_PRIVILEGED_TESTS=1 after reviewing docs/test-procedure.md" >&2; exit 2;
  }
  require docker
  require ip
  require ovs-vsctl
  require nft
  portable
  for example in macvlan ipvlan-l2 ipvlan-l3 mixed-network; do
    step "Run native $example lab"
    if test "$example" = mixed-network; then
      "$ROOT/examples/$example/scripts/linux-up.sh"
      "$ROOT/examples/$example/scripts/status.sh"
      "$ROOT/examples/$example/scripts/fault.sh" apply
      "$ROOT/examples/$example/scripts/fault.sh" clear
      "$ROOT/examples/$example/scripts/linux-down.sh"
    else
      "$ROOT/examples/$example/scripts/up.sh"
      "$ROOT/examples/$example/scripts/status.sh"
      "$ROOT/examples/$example/scripts/down.sh"
    fi
  done
  step "Privileged Linux network suite passed"
}

case "$MODE" in
  portable) portable ;;
  docker) docker_suite ;;
  linux-network) linux_network ;;
  *) echo "usage: $0 [portable|docker|linux-network]" >&2; exit 64 ;;
esac

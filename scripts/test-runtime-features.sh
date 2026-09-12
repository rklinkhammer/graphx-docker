#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
source "$ROOT/scripts/test-feature-common.sh"
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
TMP_DIR=${GRAPHX_FEATURE_TMP_DIR:-}
OWN_TMP_DIR=false
if test -z "$TMP_DIR"; then
  TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-runtime-test.XXXXXX")
  OWN_TMP_DIR=true
fi
PIDS=()

cleanup_processes() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  test "$OWN_TMP_DIR" = false || rm -rf "$TMP_DIR"
}
trap cleanup_processes EXIT INT TERM
runtime_features() {
  require cmake
  require ctest
  require node
  require openssl
  require_node24

  unset GRAPHX_CONFIG GRAPHX_OVERRIDES GRAPHX_MAX_MESSAGES GRAPHX_INTERVAL_MS

  step "Verify guided demo credential bootstrap"
  local first_demo_token second_demo_token stored_runtime
  first_demo_token=$(GRAPHX_DEMO_STATE_DIR="$TMP_DIR/demo-state" "$ROOT/scripts/demo.sh" token)
  second_demo_token=$(GRAPHX_DEMO_STATE_DIR="$TMP_DIR/demo-state" "$ROOT/scripts/demo.sh" token)
  stored_runtime=$(sed -n 's/^GRAPHX_TELEMETRY_SHARED_SECRET=//p' "$TMP_DIR/demo-state/demo.env")
  [[ "$first_demo_token" =~ ^[0-9a-f]{64}$ ]]
  test "$second_demo_token" = "$first_demo_token"
  [[ "$stored_runtime" =~ ^[0-9a-f]{64}$ ]]
  test "$stored_runtime" != "$first_demo_token"
  ls -l "$TMP_DIR/demo-state/demo.env" | grep -q '^-rw-------'
  first_demo_token=$(GRAPHX_CONTROL_TOKEN=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
    GRAPHX_TELEMETRY_SHARED_SECRET=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \
    GRAPHX_DEMO_STATE_DIR="$TMP_DIR/demo-state" "$ROOT/scripts/demo.sh" token)
  second_demo_token=$(GRAPHX_DEMO_STATE_DIR="$TMP_DIR/demo-state" "$ROOT/scripts/demo.sh" token)
  test "$first_demo_token" = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
  test "$second_demo_token" = "$first_demo_token"

  step "Configure, build, and run the C++20 suite"
  if test "$BUILD_DIR" = "$ROOT/build/dev"; then
    cmake --preset dev -S "$ROOT"
  else
    cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug -DGRAPHX_BUILD_TESTS=ON
  fi
  cmake --build "$BUILD_DIR" -j "${GRAPHX_BUILD_JOBS:-4}"
  ctest --test-dir "$BUILD_DIR" --output-on-failure -L quick

  step "Validate and inspect every checked-in topology"
  for config in "$ROOT/graphx.yaml" "$ROOT"/examples/*/graphx.yaml; do
    local example_name
    example_name=$(basename "$(dirname "$config")")
    test "$config" != "$ROOT/graphx.yaml" || example_name=root
    test "$(configuration_version "$config")" = 2
    "$BUILD_DIR/graphx" inspect "$config" >"$TMP_DIR/$example_name.inspect"
    "$BUILD_DIR/graphx" infra create "$config" --dry-run >"$TMP_DIR/$example_name.plan"
    "$BUILD_DIR/graphx" infra status "$config" --dry-run >/dev/null
    "$BUILD_DIR/graphx" infra destroy "$config" --dry-run >/dev/null
  done
  grep -q 'root netem' "$TMP_DIR/network-observability.plan"
  grep -q 'duration-seconds=30' "$TMP_DIR/network-observability.plan"
  grep -q 'auto-clear=identity-checked' "$TMP_DIR/network-observability.plan"

  step "Run the finite local TCP pipeline"
  export GRAPHX_CONFIG="$ROOT/graphx.yaml"
  export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1;observability.capture.enabled=true'
  export GRAPHX_CAPTURE_DIR="$TMP_DIR/captures"
  export GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5
  "$BUILD_DIR/graphx-sink" >"$TMP_DIR/sink.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-transform" >"$TMP_DIR/transform.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-generator" >"$TMP_DIR/generator.log" 2>&1
  wait_for_exit "${PIDS[1]}" transform
  wait_for_exit "${PIDS[0]}" sink
  PIDS=()
  grep -q 'sink seq=8 value=16' "$TMP_DIR/sink.log"
  grep -q 'event=connection state=connected' "$TMP_DIR/transform.log"
  for node in generator transform sink; do test -s "$TMP_DIR/captures/$node.pcapng"; done
  node -e '
    const fs = require("node:fs");
    for (const path of process.argv.slice(1)) {
      const capture = fs.readFileSync(path);
      if (capture.readUInt32LE(0) !== 0x0a0d0d0a ||
          !capture.includes(Buffer.from(`"wire_version":2`)) ||
          !capture.includes(Buffer.from(`"message_id":"`)) ||
          !capture.includes(Buffer.from(`"trace_id":"`)))
        throw new Error(`invalid correlated PCAPNG capture: ${path}`);
    }
  ' "$TMP_DIR"/captures/*.pcapng
  "$ROOT/tools/graphx-extcap" --extcap-interfaces >"$TMP_DIR/extcap-interfaces.txt"
  grep -q 'value=graphx' "$TMP_DIR/extcap-interfaces.txt"
  grep -q 'value=graphx-ethernet' "$TMP_DIR/extcap-interfaces.txt"
  "$ROOT/tools/graphx-extcap" --extcap-interface graphx --extcap-dlts >"$TMP_DIR/extcap-graphx-dlt.txt"
  grep -q 'number=147' "$TMP_DIR/extcap-graphx-dlt.txt"
  "$ROOT/tools/graphx-extcap" --extcap-interface graphx-ethernet --extcap-dlts >"$TMP_DIR/extcap-ethernet-dlt.txt"
  grep -q 'number=1' "$TMP_DIR/extcap-ethernet-dlt.txt"
  "$ROOT/tools/graphx-extcap" --extcap-interface graphx --extcap-config >"$TMP_DIR/extcap-config.txt"
  grep -q -- '--capture-file' "$TMP_DIR/extcap-config.txt"
  "$ROOT/tools/graphx-extcap" --extcap-interface graphx --capture \
    --capture-file "$TMP_DIR/captures/generator.pcapng" \
    --follow false --fifo "$TMP_DIR/extcap-output.pcapng"
  cmp "$TMP_DIR/captures/generator.pcapng" "$TMP_DIR/extcap-output.pcapng"
  export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
  unset GRAPHX_CAPTURE_DIR

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
  unset GRAPHX_MAX_MESSAGES GRAPHX_INTERVAL_MS

  step "Run the finite shared-memory process pipeline"
  unset GRAPHX_OVERRIDES
  GRAPHX_BUILD_DIR="$BUILD_DIR" GRAPHX_MAX_MESSAGES=8 GRAPHX_INTERVAL_MS=5 \
    "$ROOT/examples/shared-memory/run.sh" >"$TMP_DIR/shared.log"
  grep -q 'sink seq=8 value=16' "$TMP_DIR/shared.log"

  step "Run bounded UDP unicast and multicast examples"
  GRAPHX_BUILD_DIR="$BUILD_DIR" GRAPHX_MAX_MESSAGES=5 "$ROOT/examples/udp-unicast/run.sh" \
    >"$TMP_DIR/udp-unicast.log"
  grep -q 'PASS received=5' "$TMP_DIR/udp-unicast.log"
  GRAPHX_BUILD_DIR="$BUILD_DIR" GRAPHX_MAX_MESSAGES=5 "$ROOT/examples/udp-multicast/run.sh" \
    >"$TMP_DIR/udp-multicast.log"
  test "$(grep -c 'PASS received=5' "$TMP_DIR/udp-multicast.log")" = 2

  step "Runtime feature suite passed"
}

runtime_features

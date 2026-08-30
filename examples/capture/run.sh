#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
CAPTURE_DIR=${GRAPHX_CAPTURE_DIR:-"$ROOT/captures/run-$(date +%Y%m%d-%H%M%S)"}
LOG_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-capture-demo.XXXXXX")
PIDS=()

cleanup() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  rm -rf "$LOG_DIR"
}
trap cleanup EXIT INT TERM

mkdir -p "$CAPTURE_DIR"
export GRAPHX_CONFIG="$ROOT/graphx.yaml"
export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
export GRAPHX_CAPTURE_ENABLED=true GRAPHX_CAPTURE_PROVIDER=pcapng GRAPHX_CAPTURE_DIR="$CAPTURE_DIR"
export GRAPHX_MAX_MESSAGES=${GRAPHX_MAX_MESSAGES:-10}
export GRAPHX_INTERVAL_MS=${GRAPHX_INTERVAL_MS:-20}

"$BUILD_DIR/graphx-sink" >"$LOG_DIR/sink.log" 2>&1 & PIDS+=("$!")
"$BUILD_DIR/graphx-transform" >"$LOG_DIR/transform.log" 2>&1 & PIDS+=("$!")
"$BUILD_DIR/graphx-generator" >"$LOG_DIR/generator.log" 2>&1
wait "${PIDS[1]}"
wait "${PIDS[0]}"
PIDS=()

grep -q "sink seq=$GRAPHX_MAX_MESSAGES value=$((GRAPHX_MAX_MESSAGES * 2))" "$LOG_DIR/sink.log"
for node in generator transform sink; do test -s "$CAPTURE_DIR/$node.pcapng"; done

echo "GraphX capture completed: $CAPTURE_DIR"
ls -lh "$CAPTURE_DIR"/*.pcapng
echo "Open a file in Wireshark, or use capinfos/tshark to inspect LINKTYPE_USER0 records."

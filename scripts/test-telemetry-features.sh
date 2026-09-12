#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
source "$ROOT/scripts/test-feature-common.sh"
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
TMP_DIR=${GRAPHX_FEATURE_TMP_DIR:-}
OWN_TMP_DIR=false
if test -z "$TMP_DIR"; then
  TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-telemetry-test.XXXXXX")
  OWN_TMP_DIR=true
fi
PIDS=()

cleanup_processes() {
  for pid in "${PIDS[@]:-}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  test "$OWN_TMP_DIR" = false || rm -rf "$TMP_DIR"
}
trap cleanup_processes EXIT INT TERM
telemetry_features() {
  require node
  require npm
  require curl
  require openssl
  require_node24
  test -x "$BUILD_DIR/graphx" || { echo "GraphX CLI was not built at $BUILD_DIR/graphx" >&2; return 2; }
  mkdir -p "$TMP_DIR/captures"
  if test ! -s "$TMP_DIR/captures/generator.pcapng"; then
    "$BUILD_DIR/graphx-capture-fixture" "$TMP_DIR/captures/generator.pcapng"
  fi

  step "Build the web console and exercise telemetry HTTP semantics"
  npm ci --prefix "$ROOT/apps/telemetry" --no-audit --no-fund
  NORMALIZED_CONFIG_CLI="$BUILD_DIR/graphx" \
    without_graphx_environment npm test --prefix "$ROOT/apps/telemetry"
  npm ci --prefix "$ROOT/web" --no-audit --no-fund
  without_graphx_environment npm test --prefix "$ROOT/web"
  npm run build --prefix "$ROOT/web"
  sed "s|{enabled: true, provider: ovs-span}|{enabled: true, provider: pcapng, directory: $TMP_DIR/captures}|" \
    "$ROOT/examples/ipvlan-l2/graphx.yaml" >"$TMP_DIR/telemetry-graphx.yaml"
  mkdir -p "$TMP_DIR/captures"
  "$BUILD_DIR/graphx" config normalize "$TMP_DIR/telemetry-graphx.yaml" \
    >"$TMP_DIR/telemetry-normalized.json"
  export GRAPHX_TELEMETRY_SHARED_SECRET=telemetry-feature-secret-0123456789
  control_token=control-feature-token-012345678901
  PORT=${GRAPHX_TEST_HTTP_PORT:-18080} GRAPHX_TELEMETRY_PORT=${GRAPHX_TEST_UDP_PORT:-19000} \
    GRAPHX_HEARTBEAT_TIMEOUT_MS=1000 \
    GRAPHX_NORMALIZED_CONFIG="$TMP_DIR/telemetry-normalized.json" \
    GRAPHX_CONFIG_DIRECTORY="$TMP_DIR" \
    GRAPHX_CAPTURE_DIR="$TMP_DIR/captures" \
    GRAPHX_CONTROL_TOKEN="$control_token" \
    GRAPHX_WEB_ROOT="$ROOT/web/dist" node "$ROOT/apps/telemetry/server.mjs" \
    >"$TMP_DIR/telemetry.log" 2>&1 &
  PIDS+=("$!")
  for _ in {1..40}; do curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/health" >/dev/null 2>&1 && break; sleep 0.1; done
  (cd "$ROOT/apps/telemetry" && GRAPHX_WS_URL="ws://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/ws" \
    node --input-type=module -e '
      import { WebSocket } from "ws"
      const socket = new WebSocket(process.env.GRAPHX_WS_URL)
      const timeout = setTimeout(() => { socket.terminate(); process.exit(1) }, 3000)
      socket.on("message", data => {
        const snapshot = JSON.parse(data.toString())
        if (snapshot.kind !== "snapshot" || snapshot.graph !== "ipvlan-l2-pipeline" ||
            !snapshot.topology?.networkNodes?.some(node => node.id === "br-l2-gen")) process.exit(1)
        clearTimeout(timeout)
        socket.close()
      })
      socket.on("close", () => process.exit(0))
      socket.on("error", () => process.exit(1))
    ')
  GRAPHX_TEST_UDP_PORT=${GRAPHX_TEST_UDP_PORT:-19000} node -e '
    const {createHmac, randomBytes} = require("node:crypto");
    const d = require("node:dgram").createSocket("udp4");
    const secret = process.env.GRAPHX_TELEMETRY_SHARED_SECRET;
    const sign = payload => {
      const timestamp = Date.now(), nonce = randomBytes(16).toString("hex");
      const signature = createHmac("sha256", secret)
        .update(`${timestamp}.${nonce}.${JSON.stringify(payload)}`).digest("hex");
      return {payload, auth:{timestamp, nonce, signature}};
    };
    const base = {kind:"trace", nodeId:"generator", edgeId:"samples", timestamp:Date.now(), wireVersion:2,
      messageId:"00112233445566778899aabbccddeeff", parentMessageId:"", traceId:"0123456789abcdef0123456789abcdef"};
    const events = [
      {...base,event:"connection",message:"connected"},
      {...base,event:"send",sequence:1,wireBytes:64},
      {...base,event:"receive",sequence:1,wireBytes:64,latencyUs:25},
      {...base,kind:"capture",event:"frame",sequence:1,direction:"received",captureFile:"generator.pcapng",capturePacket:1,captureOffset:144},
      {...base,event:"reconnect"},
      {...base,event:"backpressure",latencyUs:40,message:"blocked"},
      {...base,event:"backpressure",latencyUs:10,message:"rejected"},
      {...base,event:"heartbeat",cpuPercent:12.5}
    ];
    d.on("message", data => {
      const command = JSON.parse(data).payload;
      if (command?.kind === "control") d.send(JSON.stringify(sign({kind:"control_ack", nodeId:"generator",
        action:command.action, commandId:command.commandId, accepted:true,
        state:command.action === "pause" ? "paused" : "running"})),
        Number(process.env.GRAPHX_TEST_UDP_PORT), "127.0.0.1");
    });
    for (const event of events) d.send(JSON.stringify(sign(event)), Number(process.env.GRAPHX_TEST_UDP_PORT), "127.0.0.1");
    setTimeout(() => d.close(), 5000);
  ' &
  PIDS+=("$!")
  sleep 0.1
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q 'ipvlan-l2-pipeline'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" >"$TMP_DIR/topology.json"
  node -e '
    const fs = require("node:fs");
    const snapshot = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
    const edge = snapshot.edges.samples;
    const failures = [];
    if (edge.sent !== 1 || edge.received !== 1) failures.push("directional message counters");
    if (edge.sentWireBytes !== 64 || edge.receivedWireBytes !== 64) failures.push("directional byte counters");
    if (edge.meanLatencyUs !== 25 || edge.p95LatencyUs !== 50) failures.push("latency summary");
    if (edge.reconnects !== 1 || edge.backpressureEvents !== 2) failures.push("pressure/reconnect counters");
    if (edge.rejected !== 1 || edge.drops !== 1) failures.push("rejection/drop counters");
    if (edge.metricSources?.counters !== "measured" || edge.metricSources?.throughput !== "derived-5s") failures.push("metric provenance");
    if (snapshot.nodes.generator.cpuPercent !== 12.5) failures.push("node CPU");
    if (snapshot.recent[0]?.messageId !== "00112233445566778899aabbccddeeff" ||
        snapshot.recent[0]?.traceId !== "0123456789abcdef0123456789abcdef") failures.push("identity correlation");
    if (snapshot.recent[0]?.captures?.[0]?.captureOffset !== 144) failures.push("capture correlation");
    if (!snapshot.capture?.enabled || snapshot.capture?.provider !== "pcapng") failures.push("capture capability");
    if (!snapshot.capture?.files?.some(file => file.name === "generator.pcapng")) failures.push("capture listing");
    if (!snapshot.control?.available || snapshot.control?.connectedNodes < 1) failures.push("control capability");
    if (failures.length) throw new Error(`bad telemetry: ${failures.join(", ")}`);
  ' "$TMP_DIR/topology.json"
  grep -q '"networkNodes"' "$TMP_DIR/topology.json"
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | grep -q 'br-l2-gen'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/metrics" >"$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_messages_total{edge="samples",direction="sent"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_messages_total{edge="samples",direction="received"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_wire_bytes_total{edge="samples",direction="sent"} 64' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_wire_bytes_total{edge="samples",direction="received"} 64' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_latency_seconds_bucket{edge="samples",le="0.00005"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_latency_seconds_sum{edge="samples"} 0.000025' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_latency_seconds_count{edge="samples"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_backpressure_events_total{edge="samples"} 2' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_rejected_total{edge="samples"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_edge_dropped_total{edge="samples"} 1' "$TMP_DIR/metrics.txt"
  grep -q 'graphx_node_cpu_percent{node="generator"} 12.5' "$TMP_DIR/metrics.txt"
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/captures" | grep -q 'generator.pcapng'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/captures/generator.pcapng" \
    >"$TMP_DIR/downloaded.pcapng"
  cmp "$TMP_DIR/captures/generator.pcapng" "$TMP_DIR/downloaded.pcapng"
  test "$(curl -sS -o "$TMP_DIR/pause-unauthorized.json" -w '%{http_code}' -X POST "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/pause")" = 401
  grep -q '"accepted":false' "$TMP_DIR/pause-unauthorized.json"
  test "$(curl -sS -o "$TMP_DIR/pause.json" -w '%{http_code}' -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/pause")" = 202
  grep -q '"accepted":true' "$TMP_DIR/pause.json"
  pause_id=$(node -p 'JSON.parse(require("node:fs").readFileSync(process.argv[1], "utf8")).command.id' "$TMP_DIR/pause.json")
  command_accepted=false
  for _ in {1..40}; do
    curl -fsS -H "Authorization: Bearer $control_token" \
      "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/commands/$pause_id" \
      >"$TMP_DIR/pause-status.json"
    if node -e 'const value=require(process.argv[1]); process.exit(value.status === "accepted" ? 0 : 1)' \
        "$TMP_DIR/pause-status.json"; then
      command_accepted=true
      break
    fi
    sleep 0.05
  done
  test "$command_accepted" = true
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" \
    >"$TMP_DIR/topology-after-control.json"
  if grep -q '"commands"' "$TMP_DIR/topology-after-control.json"; then
    echo "observation topology exposed control command summaries" >&2
    return 1
  fi
  curl -fsS -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/reset" | grep -q '"paused":true'
  test "$(curl -sS -o "$TMP_DIR/resume.json" -w '%{http_code}' -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/resume")" = 202
  grep -q '"accepted":true' "$TMP_DIR/resume.json"

  step "Verify authenticated pause and resume against the real TCP runtime"
  export GRAPHX_CONFIG="$ROOT/graphx.yaml"
  export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
  export GRAPHX_TELEMETRY_HOST=127.0.0.1
  export GRAPHX_TELEMETRY_PORT=${GRAPHX_TEST_UDP_PORT:-19000}
  export GRAPHX_MAX_MESSAGES=0 GRAPHX_INTERVAL_MS=25
  "$BUILD_DIR/graphx-sink" >"$TMP_DIR/control-sink.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-transform" >"$TMP_DIR/control-transform.log" 2>&1 & PIDS+=("$!")
  "$BUILD_DIR/graphx-generator" >"$TMP_DIR/control-generator.log" 2>&1 & PIDS+=("$!")
  sent_count() {
    curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | \
      node -e 'let value=""; process.stdin.on("data", chunk => value += chunk).on("end", () => console.log(JSON.parse(value).edges.samples.sent))'
  }
  for _ in {1..40}; do test "$(sent_count)" -ge 3 && break; sleep 0.05; done
  test "$(curl -sS -o "$TMP_DIR/runtime-pause.json" -w '%{http_code}' -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/pause")" = 202
  sleep 0.15
  paused_count=$(sent_count)
  # Stay paused beyond the collector's one-second endpoint timeout. Runtime
  # heartbeats must keep the control return path live so Resume still works.
  sleep 1.25
  test "$(sent_count)" = "$paused_count"
  test "$(curl -sS -o "$TMP_DIR/runtime-resume.json" -w '%{http_code}' -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/resume")" = 202
  resumed=false
  for _ in {1..40}; do
    if test "$(sent_count)" -gt "$paused_count"; then resumed=true; break; fi
    sleep 0.05
  done
  test "$resumed" = true
  kill -TERM "${PIDS[4]}" "${PIDS[3]}" "${PIDS[2]}"
  wait_for_exit "${PIDS[4]}" generator
  wait_for_exit "${PIDS[3]}" transform
  wait_for_exit "${PIDS[2]}" sink
  PIDS=("${PIDS[0]}" "${PIDS[1]}")
  unset GRAPHX_TELEMETRY_HOST GRAPHX_TELEMETRY_PORT

  curl -fsS -X POST -H "Authorization: Bearer $control_token" "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/control/reset" | grep -q '"accepted":true'
  curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/metrics" | grep -q 'graphx_edge_messages_total{edge="samples",direction="sent"} 0'
  offline=false
  for _ in {1..30}; do
    if curl -fsS "http://127.0.0.1:${GRAPHX_TEST_HTTP_PORT:-18080}/api/topology" | \
        grep -q '"status":"offline"'; then
      offline=true
      break
    fi
    sleep 0.1
  done
  test "$offline" = true

  step "Verify HTTPS, observation authentication, API methods, and secure bind defaults"
  kill -TERM "${PIDS[0]}" 2>/dev/null || true
  # Node uses the default SIGTERM disposition, so an intentional collector stop
  # reports 143 rather than the native demo processes' graceful zero exit.
  wait "${PIDS[0]}" 2>/dev/null || true
  wait "${PIDS[1]}" 2>/dev/null || true
  PIDS=()
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 -subj '/CN=127.0.0.1' \
    -addext 'subjectAltName=IP:127.0.0.1' -keyout "$TMP_DIR/http.key" -out "$TMP_DIR/http.pem" \
    >/dev/null 2>&1
  secure_http_port=$(( ${GRAPHX_TEST_HTTP_PORT:-18080} + 1 ))
  secure_udp_port=$(( ${GRAPHX_TEST_UDP_PORT:-19000} + 1 ))
  observation_token=observation-feature-token-012345678
  PORT=$secure_http_port GRAPHX_TELEMETRY_PORT=$secure_udp_port \
    GRAPHX_NORMALIZED_CONFIG="$TMP_DIR/telemetry-normalized.json" \
    GRAPHX_CONFIG_DIRECTORY="$TMP_DIR" GRAPHX_WEB_ROOT="$ROOT/web/dist" \
    GRAPHX_CAPTURE_DIR="$TMP_DIR/captures" \
    GRAPHX_TLS_CERT_FILE="$TMP_DIR/http.pem" GRAPHX_TLS_KEY_FILE="$TMP_DIR/http.key" \
    GRAPHX_OBSERVATION_TOKEN="$observation_token" node "$ROOT/apps/telemetry/server.mjs" \
    >"$TMP_DIR/secure-telemetry.log" 2>&1 &
  PIDS+=("$!")
  for _ in {1..40}; do curl -kfsS "https://127.0.0.1:$secure_http_port/api/health" >/dev/null 2>&1 && break; sleep 0.1; done
  test "$(curl -ksS -o /dev/null -w '%{http_code}' "https://127.0.0.1:$secure_http_port/api/topology")" = 401
  curl -kfsS -H "Authorization: Bearer $observation_token" \
    "https://127.0.0.1:$secure_http_port/api/topology" | grep -q 'ipvlan-l2-pipeline'
  test "$(curl -ksS -o /dev/null -w '%{http_code}' -X POST \
    -H "Authorization: Bearer $observation_token" \
    "https://127.0.0.1:$secure_http_port/api/topology")" = 405
  curl -kisS "https://127.0.0.1:$secure_http_port/api/health" | \
    grep -qi '^x-content-type-options: nosniff'
  if PORT=$((secure_http_port + 1)) GRAPHX_TELEMETRY_PORT=$((secure_udp_port + 1)) \
    GRAPHX_HTTP_BIND=0.0.0.0 \
    GRAPHX_NORMALIZED_CONFIG="$TMP_DIR/telemetry-normalized.json" \
    GRAPHX_CONFIG_DIRECTORY="$TMP_DIR" \
    GRAPHX_CAPTURE_DIR="$TMP_DIR/captures" \
    node "$ROOT/apps/telemetry/server.mjs" >"$TMP_DIR/insecure-bind.log" 2>&1; then
    echo "plaintext non-loopback telemetry bind was accepted" >&2
    return 1
  fi
  grep -q 'plaintext telemetry may bind only to loopback' "$TMP_DIR/insecure-bind.log"

  step "Portable feature suite passed"
}

telemetry_features

#!/usr/bin/env bash
set -euo pipefail
umask 077

profile_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
example_dir=$(cd "$profile_dir/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
source "$repo_dir/scripts/configure-build-trust.sh"
state_dir="$example_dir/.state"
state_file="$state_dir/simulated.env"
GRAPHX_SDR_GUI_PORT=${GRAPHX_SDR_GUI_PORT:-8080}
url="http://127.0.0.1:$GRAPHX_SDR_GUI_PORT"
COMPOSE=(docker compose -f "$profile_dir/compose.yaml")

usage() {
  cat <<EOF
Usage: examples/sdr-node/simulated/scripts/demo.sh <start|verify|status|logs|token|control|stop> [options]

  start                  Start the portable simulated-SDR profile
  verify                 Verify traffic, TLS control, capture, history, and GUI API
  status                 Show services and the SDR's authenticated status
  logs                   Follow recent demo output
  token                  Print the browser control token
  control status         Query SDR state over mutual TLS
  control start|stop     Start or stop SDR sample transmission
  control tune HERTZ     Change the simulated center frequency
  stop                   Stop services; retain run evidence

Start options: --no-capture --no-history
EOF
}
require() { command -v "$1" >/dev/null 2>&1 || { echo "Missing prerequisite: $1" >&2; exit 2; }; }
preflight_port() {
  python3 - "${GRAPHX_SDR_GUI_PORT:-8080}" <<'PY'
import socket, sys, time
port=int(sys.argv[1])
if not 1 <= port <= 65535: raise SystemExit("GRAPHX_SDR_GUI_PORT must be from 1 through 65535")
error=None
for _ in range(50):
  with socket.socket() as candidate:
    try:
        candidate.bind(("127.0.0.1", port))
        break
    except OSError as error:
        time.sleep(.1)
else:
  raise SystemExit(f"SDR GUI loopback port {port} is unavailable: {error}")
PY
}
packet_rules() {
  printf '%s' '[{"edge_id":"sdr-samples","direction":"sdr-to-processor","node_id":"sdr-node","protocol":"UDP","source":"172.30.13.10","destination":"172.30.13.20","destination_port":18400},{"edge_id":"processor-control","direction":"processor-to-sdr","node_id":"processor","protocol":"TCP","source":"172.30.13.20","destination":"172.30.13.10","destination_port":18401},{"edge_id":"processed-results","direction":"processor-to-sink","node_id":"sink","protocol":"TCP","source":"172.30.13.20","destination":"172.30.13.30","destination_port":18402}]'
}
load_state() {
  test -r "$state_file" || { echo "No simulated SDR state; run start first" >&2; return 2; }
  # Generated locally with fixed names; paths are checked before use.
  # shellcheck disable=SC1090
  source "$state_file"
  GRAPHX_SDR_GUI_PORT=${GRAPHX_SDR_GUI_PORT:-8080}
  url="http://127.0.0.1:$GRAPHX_SDR_GUI_PORT"
  case "$GRAPHX_SDR_RUN_DIR" in "$repo_dir"/outputs/sdr-node/simulated/*) ;; *) echo "Unsafe run directory" >&2; return 2 ;; esac
  case "$GRAPHX_SDR_TLS_DIR" in "$state_dir"/simulated-tls) ;; *) echo "Unsafe TLS directory" >&2; return 2 ;; esac
  export GRAPHX_CONTROL_TOKEN GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_SDR_RUN_ID
  export GRAPHX_SDR_RUN_DIR GRAPHX_SDR_TLS_DIR GRAPHX_CAPTURE_ENABLED GRAPHX_HISTORY_ENABLED
  GRAPHX_HOST_UID=$(id -u); GRAPHX_HOST_GID=$(id -g)
  export GRAPHX_HOST_UID GRAPHX_HOST_GID
  export GRAPHX_SDR_GUI_PORT
  export GRAPHX_SDR_PACKET_RULES="$(packet_rules)"
}
create_state() {
  require openssl
  mkdir -p "$state_dir" "$repo_dir/outputs/sdr-node/simulated"
  chmod 0700 "$state_dir"
  GRAPHX_SDR_RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
  GRAPHX_SDR_RUN_DIR="$repo_dir/outputs/sdr-node/simulated/$GRAPHX_SDR_RUN_ID"
  GRAPHX_SDR_TLS_DIR="$state_dir/simulated-tls"
  GRAPHX_CONTROL_TOKEN=$(openssl rand -hex 32)
  GRAPHX_TELEMETRY_SHARED_SECRET=$(openssl rand -hex 32)
  mkdir -p "$GRAPHX_SDR_RUN_DIR"
  "$example_dir/common/generate_tls.sh" "$GRAPHX_SDR_TLS_DIR"
  temporary="$state_file.tmp.$$"
  printf 'GRAPHX_CONTROL_TOKEN=%q\nGRAPHX_TELEMETRY_SHARED_SECRET=%q\nGRAPHX_SDR_RUN_ID=%q\nGRAPHX_SDR_RUN_DIR=%q\nGRAPHX_SDR_TLS_DIR=%q\nGRAPHX_SDR_GUI_PORT=%q\nGRAPHX_CAPTURE_ENABLED=%q\nGRAPHX_HISTORY_ENABLED=%q\n' \
    "$GRAPHX_CONTROL_TOKEN" "$GRAPHX_TELEMETRY_SHARED_SECRET" "$GRAPHX_SDR_RUN_ID" \
    "$GRAPHX_SDR_RUN_DIR" "$GRAPHX_SDR_TLS_DIR" "$GRAPHX_SDR_GUI_PORT" "$GRAPHX_CAPTURE_ENABLED" \
    "$GRAPHX_HISTORY_ENABLED" >"$temporary"
  chmod 0600 "$temporary"
  mv "$temporary" "$state_file"
  load_state
}
wait_ready() {
  printf 'Waiting for SDR telemetry'
  for _ in {1..60}; do
    if curl -fsS "$url/api/ready" >/dev/null 2>&1; then printf ' ready\n'; return; fi
    printf '.'; sleep 1
  done
  printf ' timed out\n' >&2
  "${COMPOSE[@]}" ps >&2
  return 1
}
control() {
  local action=${1:-status}
  shift || true
  case "$action" in
    status|start|stop) test "$#" -eq 0 || { usage; return 64; } ;;
    tune) test "$#" -eq 1 && [[ "$1" =~ ^[0-9]+$ ]] || { usage; return 64; } ;;
    *) usage; return 64 ;;
  esac
  if test "$action" = tune; then
    "${COMPOSE[@]}" exec -T processor python3 common/sdrctl.py tune "$1"
  else
    "${COMPOSE[@]}" exec -T processor python3 common/sdrctl.py "$action"
  fi
}
verify() {
  require curl
  wait_ready
  local running first second history
  running=$("${COMPOSE[@]}" ps --status running --services)
  for service in sdr-node processor sink packet-capture packet-observer telemetry; do
    grep -qx "$service" <<<"$running" || { echo "FAIL: $service is not running" >&2; return 1; }
  done
  control status >/dev/null
  first=$(curl -fsS "$url/api/topology")
  sleep 2
  second=$(curl -fsS "$url/api/topology")
  python3 - "$first" "$second" <<'PY'
import json, sys
first, second = map(json.loads, sys.argv[1:])
def packets(value):
    return sum(edge.get("received", 0) for edge in value.get("edges", {}).values())
if packets(second) <= packets(first):
    raise SystemExit("FAIL: live packet counters did not advance")
print(f"PASS: live packet counters advanced {packets(first)} -> {packets(second)}")
PY
  if test "$GRAPHX_HISTORY_ENABLED" = true; then
    history=$(curl -fsS "$url/api/packet-history?limit=10")
    python3 - "$history" <<'PY'
import json, sys
value=json.loads(sys.argv[1])
if not value.get("records"): raise SystemExit("FAIL: packet history has no records")
print(f"PASS: packet history contains {len(value['records'])} queried records")
PY
  fi
  if test "$GRAPHX_CAPTURE_ENABLED" = true; then
    test -s "$GRAPHX_SDR_RUN_DIR/sdr-node.pcapng" || { echo "FAIL: PCAPNG is missing" >&2; return 1; }
    echo "PASS: bounded Ethernet PCAPNG exists"
  fi
  curl -fsS -X POST -H "Authorization: Bearer $GRAPHX_CONTROL_TOKEN" \
    "$url/api/control/pause" >/dev/null
  sleep 1
  control status | grep -q '"running": false' || { echo "FAIL: GUI pause did not stop the SDR" >&2; return 1; }
  curl -fsS -X POST -H "Authorization: Bearer $GRAPHX_CONTROL_TOKEN" \
    "$url/api/control/resume" >/dev/null
  sleep 1
  control status | grep -q '"running": true' || { echo "FAIL: GUI resume did not start the SDR" >&2; return 1; }
  echo "PASS: GUI pause/resume relays through processor mTLS control"
  echo "PASS: mutual-TLS SDR control responds"
  echo "PASS: all six services are running"
  echo "Console: $url"
}

command_name=${1:-}
if test -n "$command_name"; then shift; fi
disable_capture=false
disable_history=false
while test "$#" -gt 0 && [[ "$1" == --* ]]; do
  case "$1" in --no-capture) disable_capture=true ;; --no-history) disable_history=true ;; *) usage; exit 64 ;; esac
  shift
done
case "$command_name" in
  start)
    require docker; require curl; require python3
    if test -r "$state_file"; then
      load_state
      "${COMPOSE[@]}" down --remove-orphans
    fi
    preflight_port
    GRAPHX_CAPTURE_ENABLED=true; GRAPHX_HISTORY_ENABLED=true
    test "$disable_capture" = false || GRAPHX_CAPTURE_ENABLED=false
    test "$disable_history" = false || GRAPHX_HISTORY_ENABLED=false
    create_state
    "${COMPOSE[@]}" up -d --build
    verify
    echo "Control token: $GRAPHX_CONTROL_TOKEN"
    ;;
  verify) load_state; require docker; verify ;;
  status) load_state; "${COMPOSE[@]}" ps; control status ;;
  logs) load_state; "${COMPOSE[@]}" logs -f --tail=40 ;;
  token) load_state; printf '%s\n' "$GRAPHX_CONTROL_TOKEN" ;;
  control) load_state; control "$@" ;;
  stop) load_state; "${COMPOSE[@]}" down --remove-orphans ;;
  *) usage; exit 64 ;;
esac

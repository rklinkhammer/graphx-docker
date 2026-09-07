#!/usr/bin/env bash
set -euo pipefail
umask 077

profile_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
example_dir=$(cd "$profile_dir/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
source "$repo_dir/scripts/configure-build-trust.sh"
source "$example_dir/common/lifecycle.sh"
state_dir="$example_dir/.state"
state_file="$state_dir/external.env"
namespace=gx-sdr-device
GRAPHX_SDR_GUI_PORT=${GRAPHX_SDR_GUI_PORT:-8080}
url="http://127.0.0.1:$GRAPHX_SDR_GUI_PORT"
COMPOSE=(docker compose -f "$profile_dir/compose.yaml")
current_start_owns_native=false

usage() {
  cat <<EOF
Usage: examples/sdr-node/external/scripts/demo.sh <start|verify|status|logs|token|control|stop> [options]

This native-Linux profile places an external SDR simulator behind Open vSwitch.
GraphX never owns or configures a physical SDR; see the profile README for that contract.

Commands: start, verify, status, logs, token, control status|start|stop|tune HERTZ, stop
Start options: --no-capture --no-history
EOF
}
require() { command -v "$1" >/dev/null 2>&1 || { echo "Missing prerequisite: $1" >&2; exit 2; }; }
find_graphx() {
  local candidate
  if test -n "${GRAPHX_BIN:-}" && test -x "$GRAPHX_BIN"; then printf '%s\n' "$GRAPHX_BIN"; return; fi
  for candidate in "$repo_dir/build/dev/graphx" "$repo_dir/build/graphx" "$repo_dir/build"/*/graphx; do
    if test -x "$candidate"; then printf '%s\n' "$candidate"; return; fi
  done
  echo "GraphX CLI not found; set GRAPHX_BIN or build a CMake preset first" >&2
  return 2
}
packet_rules() {
  printf '%s' '[{"edge_id":"sdr-samples","direction":"sdr-to-processor","node_id":"sdr-node","protocol":"UDP","source":"10.63.0.10","destination":"10.63.0.20","destination_port":18400},{"edge_id":"processor-control","direction":"processor-to-sdr","node_id":"processor","protocol":"TCP","source":"10.63.0.20","destination":"10.63.0.10","destination_port":18401},{"edge_id":"processed-results","direction":"processor-to-sink","node_id":"sink","protocol":"TCP","source":"10.63.0.20","destination":"10.63.0.30","destination_port":18402}]'
}
load_state() {
  test -r "$state_file" || { echo "No external SDR state; run start first" >&2; return 2; }
  # shellcheck disable=SC1090
  source "$state_file"
  GRAPHX_SDR_GUI_PORT=${GRAPHX_SDR_GUI_PORT:-8080}
  url="http://127.0.0.1:$GRAPHX_SDR_GUI_PORT"
  case "$GRAPHX_SDR_RUN_DIR" in "$repo_dir"/outputs/sdr-node/external/*) ;; *) echo "Unsafe run directory" >&2; return 2 ;; esac
  case "$GRAPHX_SDR_TLS_DIR" in "$state_dir"/external-tls) ;; *) echo "Unsafe TLS directory" >&2; return 2 ;; esac
  GRAPHX_HOST_UID=$(id -u); GRAPHX_HOST_GID=$(id -g)
  GRAPHX_SDR_PACKET_RULES=$(packet_rules)
  export GRAPHX_CONTROL_TOKEN GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_SDR_RUN_ID
  export GRAPHX_SDR_RUN_DIR GRAPHX_SDR_TLS_DIR GRAPHX_CAPTURE_ENABLED GRAPHX_HISTORY_ENABLED
  export GRAPHX_HOST_UID GRAPHX_HOST_GID GRAPHX_SDR_PACKET_RULES
  export GRAPHX_SDR_GUI_PORT
}
create_state() {
  require openssl
  mkdir -p "$state_dir" "$repo_dir/outputs/sdr-node/external"
  chmod 0700 "$state_dir"
  GRAPHX_SDR_RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
  GRAPHX_SDR_RUN_DIR="$repo_dir/outputs/sdr-node/external/$GRAPHX_SDR_RUN_ID"
  GRAPHX_SDR_TLS_DIR="$state_dir/external-tls"
  GRAPHX_CONTROL_TOKEN=$(openssl rand -hex 32)
  GRAPHX_TELEMETRY_SHARED_SECRET=$(openssl rand -hex 32)
  GRAPHX_SDR_OWNER=$(openssl rand -hex 16)
  mkdir -p "$GRAPHX_SDR_RUN_DIR"
  chmod 0770 "$GRAPHX_SDR_RUN_DIR"
  "$example_dir/common/generate_tls.sh" "$GRAPHX_SDR_TLS_DIR"
  temporary="$state_file.tmp.$$"
  printf 'GRAPHX_CONTROL_TOKEN=%q\nGRAPHX_TELEMETRY_SHARED_SECRET=%q\nGRAPHX_SDR_RUN_ID=%q\nGRAPHX_SDR_RUN_DIR=%q\nGRAPHX_SDR_TLS_DIR=%q\nGRAPHX_SDR_GUI_PORT=%q\nGRAPHX_CAPTURE_ENABLED=%q\nGRAPHX_HISTORY_ENABLED=%q\nGRAPHX_SDR_OWNER=%q\n' \
    "$GRAPHX_CONTROL_TOKEN" "$GRAPHX_TELEMETRY_SHARED_SECRET" "$GRAPHX_SDR_RUN_ID" \
    "$GRAPHX_SDR_RUN_DIR" "$GRAPHX_SDR_TLS_DIR" "$GRAPHX_SDR_GUI_PORT" "$GRAPHX_CAPTURE_ENABLED" \
    "$GRAPHX_HISTORY_ENABLED" "$GRAPHX_SDR_OWNER" >"$temporary"
  chmod 0600 "$temporary"; mv "$temporary" "$state_file"; load_state
}
owned_pid() {
  local file=$1 marker=$2 pid command
  test -r "$file" || return 1
  pid=$(cat "$file"); [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *"$marker"* ]]
}
stop_owned() {
  local file=$1 marker=$2 signal=${3:-TERM} pid
  owned_pid "$file" "$marker" || return 0
  pid=$(cat "$file"); sudo kill -s "$signal" "$pid" 2>/dev/null || true
  for _ in {1..50}; do kill -0 "$pid" 2>/dev/null || return 0; sleep 0.1; done
  sudo kill -KILL "$pid" 2>/dev/null || true
}
create_native_network() {
  local graphx=$1
  sudo "$graphx" infra create "$profile_dir/graphx.yaml"
  local owner_tag="graphx-sdr:$GRAPHX_SDR_OWNER" name
  # The general infrastructure planner cannot receive a per-run label. Replace
  # only the network created by this attempt, before any container attachment,
  # so later cleanup can prove ownership from Docker itself.
  docker network rm gx-sdr-native >/dev/null
  docker network create --driver macvlan --subnet 10.63.0.0/24 --gateway 10.63.0.1 \
    --opt parent=sdrp-parent --label "com.graphx.sdr.owner=$GRAPHX_SDR_OWNER" \
    gx-sdr-native >/dev/null
  sudo ovs-vsctl set Bridge br-sdr external_ids:graphx_sdr_owner="$GRAPHX_SDR_OWNER"
  sudo ovs-vsctl set Mirror mirror-sdr external_ids:graphx_sdr_owner="$GRAPHX_SDR_OWNER"
  for name in sdrp-ovs sdrp-parent sdr-dev-ovs sdr-dev sdr-cap-ovs sdr-cap; do
    sudo ip link set dev "$name" alias "$owner_tag"
  done
  sudo ip netns add "$namespace"
  sudo ip link set sdr-dev netns "$namespace"
  sudo ip netns exec "$namespace" ip link set lo up
  sudo ip netns exec "$namespace" ip link set lo alias "$owner_tag"
  sudo ip netns exec "$namespace" ip link set sdr-dev name eth0
  sudo ip netns exec "$namespace" ip link set eth0 address 02:63:00:00:00:10
  sudo ip netns exec "$namespace" ip address add 10.63.0.10/24 dev eth0
  sudo ip netns exec "$namespace" ip link set eth0 up
  sudo ip link set sdr-cap up
}
native_resources_exist() {
  local name
  for name in br-sdr sdrp-ovs sdrp-parent sdr-dev-ovs sdr-dev sdr-cap-ovs sdr-cap; do
    ip link show "$name" >/dev/null 2>&1 && return 0
  done
  sudo ip netns list 2>/dev/null | awk '{print $1}' | grep -qx "$namespace" && return 0
  docker network inspect gx-sdr-native >/dev/null 2>&1 && return 0
  return 1
}
native_resources_owned() {
  local owner_tag="graphx-sdr:$GRAPHX_SDR_OWNER" name value
  test -n "${GRAPHX_SDR_OWNER:-}" || return 1
  if ip link show br-sdr >/dev/null 2>&1; then
    value=$(sudo ovs-vsctl --if-exists get Bridge br-sdr external_ids:graphx_sdr_owner | tr -d '"')
    test "$value" = "$GRAPHX_SDR_OWNER" || return 1
    value=$(sudo ovs-vsctl --if-exists get Mirror mirror-sdr external_ids:graphx_sdr_owner | tr -d '"')
    test "$value" = "$GRAPHX_SDR_OWNER" || return 1
  fi
  for name in sdrp-ovs sdrp-parent sdr-dev-ovs sdr-dev sdr-cap-ovs sdr-cap; do
    if ip link show "$name" >/dev/null 2>&1; then
      ip -d link show "$name" | grep -Fq "alias $owner_tag" || return 1
    fi
  done
  if sudo ip netns list 2>/dev/null | awk '{print $1}' | grep -qx "$namespace"; then
    sudo ip netns exec "$namespace" ip -d link show lo | grep -Fq "alias $owner_tag" || return 1
    sudo ip netns exec "$namespace" ip -d link show eth0 | grep -Fq "alias $owner_tag" || return 1
  fi
  if docker network inspect gx-sdr-native >/dev/null 2>&1; then
    value=$(docker network inspect -f '{{ index .Labels "com.graphx.sdr.owner" }}' gx-sdr-native)
    test "$value" = "$GRAPHX_SDR_OWNER" || return 1
  fi
}
preflight_native_names() {
  local name
  for name in br-sdr sdrp-ovs sdrp-parent sdr-dev-ovs sdr-dev sdr-cap-ovs sdr-cap; do
    if ip link show "$name" >/dev/null 2>&1; then
      echo "Native SDR lab interface already exists: $name" >&2
      return 1
    fi
  done
  if sudo ip netns list | awk '{print $1}' | grep -qx "$namespace"; then
    echo "Native SDR lab namespace already exists: $namespace" >&2
    return 1
  fi
  if docker network inspect gx-sdr-native >/dev/null 2>&1; then
    echo "Native SDR Docker network already exists: gx-sdr-native" >&2
    return 1
  fi
}
start_capture() {
  sudo sh -c 'echo $$ > "$1"; chown "$3:$4" "$1"; chmod 0600 "$1"; exec tcpdump -Z "$5" -U -n -i sdr-cap -s 65535 -C 64 -W 1 -w "$2" "udp port 18400 or tcp port 18401 or tcp port 18402"' \
    sh "$GRAPHX_SDR_RUN_DIR/capture.pid" "$GRAPHX_SDR_RUN_DIR/sdr-node.pcap" \
    "$(id -u)" "$(id -g)" "$(id -un)" \
    >"$GRAPHX_SDR_RUN_DIR/tcpdump.log" 2>&1 &
  for _ in {1..50}; do owned_pid "$GRAPHX_SDR_RUN_DIR/capture.pid" "$GRAPHX_SDR_RUN_DIR/sdr-node.pcap" && return; sleep 0.1; done
  echo "tcpdump failed to start on the OVS mirror port" >&2; return 1
}
start_external_sdr() {
  sudo sh -c 'echo $$ > "$1"; chown "$5:$6" "$1"; chmod 0600 "$1"; exec ip netns exec gx-sdr-device env PYTHONPATH="$2/common" SDR_SAMPLE_TARGET=10.63.0.20 SDR_TLS_CERT="$3/sdr-node.pem" SDR_TLS_KEY="$3/sdr-node.key" SDR_TLS_CLIENT_CA="$3/ca.pem" GRAPHX_TELEMETRY_HOST=10.63.0.40 GRAPHX_TELEMETRY_PORT=9000 GRAPHX_TELEMETRY_SHARED_SECRET="$4" python3 "$2/common/sdr_simulator.py"' \
    sh "$GRAPHX_SDR_RUN_DIR/sdr.pid" "$example_dir" "$GRAPHX_SDR_TLS_DIR" "$GRAPHX_TELEMETRY_SHARED_SECRET" \
    "$(id -u)" "$(id -g)" \
    >"$GRAPHX_SDR_RUN_DIR/sdr.log" 2>&1 &
  for _ in {1..50}; do owned_pid "$GRAPHX_SDR_RUN_DIR/sdr.pid" sdr_simulator.py && return; sleep 0.1; done
  echo "external SDR simulator failed to start" >&2; return 1
}
wait_ready() {
  printf 'Waiting for external SDR telemetry'
  for _ in {1..60}; do
    if curl -fsS "$url/api/ready" >/dev/null 2>&1; then printf ' ready\n'; return; fi
    printf '.'; sleep 1
  done
  printf ' timed out\n' >&2; return 1
}
control() {
  local action=${1:-status}; shift || true
  case "$action" in status|start|stop) test "$#" -eq 0 || { usage; return 64; } ;;
    tune) test "$#" -eq 1 && [[ "$1" =~ ^[0-9]+$ ]] || { usage; return 64; } ;;
    *) usage; return 64 ;; esac
  if test "$action" = tune; then "${COMPOSE[@]}" exec -T processor python3 common/sdrctl.py tune "$1"
  else "${COMPOSE[@]}" exec -T processor python3 common/sdrctl.py "$action"; fi
}
verify() {
  wait_ready
  owned_pid "$GRAPHX_SDR_RUN_DIR/sdr.pid" sdr_simulator.py || { echo "FAIL: external SDR process is not owned/live" >&2; return 1; }
  owned_pid "$GRAPHX_SDR_RUN_DIR/capture.pid" "$GRAPHX_SDR_RUN_DIR/sdr-node.pcap" || { echo "FAIL: capture is not owned/live" >&2; return 1; }
  control status >/dev/null
  local first second history running
  running=$("${COMPOSE[@]}" ps --status running --services)
  for service in processor sink packet-observer telemetry; do grep -qx "$service" <<<"$running" || { echo "FAIL: $service is not running" >&2; return 1; }; done
  first=$(curl -fsS "$url/api/topology"); sleep 2; second=$(curl -fsS "$url/api/topology")
  python3 - "$first" "$second" <<'PY'
import json, sys
a,b=map(json.loads,sys.argv[1:])
count=lambda v: sum(e.get("received",0) for e in v.get("edges",{}).values())
if count(b)<=count(a): raise SystemExit("FAIL: OVS-observed counters did not advance")
print(f"PASS: OVS-observed counters advanced {count(a)} -> {count(b)}")
PY
  if test "$GRAPHX_HISTORY_ENABLED" = true; then
    history=$(curl -fsS "$url/api/packet-history?limit=10")
    python3 - "$history" <<'PY'
import json,sys
if not json.loads(sys.argv[1]).get("records"): raise SystemExit("FAIL: packet history is empty")
print("PASS: separate packet history is queryable")
PY
  fi
  test "$GRAPHX_CAPTURE_ENABLED" = false || test -s "$GRAPHX_SDR_RUN_DIR/sdr-node.pcapng" || { echo "FAIL: PCAPNG is missing" >&2; return 1; }
  curl -fsS -X POST -H "Authorization: Bearer $GRAPHX_CONTROL_TOKEN" "$url/api/control/pause" >/dev/null
  sleep 1
  control status | grep -q '"running": false' || { echo "FAIL: GUI pause did not stop the SDR" >&2; return 1; }
  curl -fsS -X POST -H "Authorization: Bearer $GRAPHX_CONTROL_TOKEN" "$url/api/control/resume" >/dev/null
  sleep 1
  control status | grep -q '"running": true' || { echo "FAIL: GUI resume did not start the SDR" >&2; return 1; }
  echo "PASS: GUI pause/resume relays through processor mTLS control"
  echo "PASS: external SDR, OVS/SPAN, UDP samples, TLS control, results, and GUI API are live"
}
cleanup() (
  set +e
  test -r "$state_file" || return 0
  load_state >/dev/null 2>&1 || return 2
  if native_resources_exist && test "$current_start_owns_native" != true && ! native_resources_owned; then
    echo "Refusing to remove native SDR resources without matching per-run ownership markers" >&2
    return 2
  fi
  "${COMPOSE[@]}" down --remove-orphans >/dev/null 2>&1
  if test -n "${GRAPHX_SDR_RUN_DIR:-}"; then
    stop_owned "$GRAPHX_SDR_RUN_DIR/sdr.pid" sdr_simulator.py TERM
    stop_owned "$GRAPHX_SDR_RUN_DIR/capture.pid" "$GRAPHX_SDR_RUN_DIR/sdr-node.pcap" INT
  fi
  local graphx; graphx=$(find_graphx 2>/dev/null) || graphx=
  test -z "$graphx" || sudo "$graphx" infra destroy "$profile_dir/graphx.yaml" >/dev/null 2>&1
  sudo ip netns delete "$namespace" >/dev/null 2>&1
  return 0
)
rollback_start() {
  local status=$?
  echo "start failed; rolling back owned resources" >&2
  cleanup
  return "$status"
}

command_name=${1:-}; if test -n "$command_name"; then shift; fi
disable_capture=false; disable_history=false
while test "$#" -gt 0 && [[ "$1" == --* ]]; do
  case "$1" in --no-capture) disable_capture=true ;; --no-history) disable_history=true ;; *) usage; exit 64 ;; esac; shift
done
case "$command_name" in
  start)
    test "$(uname -s)" = Linux || { echo "The OVS external profile requires native Linux" >&2; exit 2; }
    for tool in docker ovs-vsctl ip tcpdump sudo curl python3 openssl; do require "$tool"; done
    graphx=$(find_graphx)
    requested_gui_port=$GRAPHX_SDR_GUI_PORT
    trap rollback_start EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    if test -r "$state_file"; then cleanup; fi
    GRAPHX_SDR_GUI_PORT=$requested_gui_port
    url="http://127.0.0.1:$GRAPHX_SDR_GUI_PORT"
    export GRAPHX_SDR_GUI_PORT
    GRAPHX_CAPTURE_ENABLED=true; GRAPHX_HISTORY_ENABLED=true
    test "$disable_capture" = false || GRAPHX_CAPTURE_ENABLED=false
    test "$disable_history" = false || GRAPHX_HISTORY_ENABLED=false
    graphx_sdr_preflight_port "$GRAPHX_SDR_GUI_PORT"; preflight_native_names; create_state
    current_start_owns_native=true
    create_native_network "$graphx"; start_capture; start_external_sdr
    "${COMPOSE[@]}" up -d --build
    verify
    trap - EXIT INT TERM
    echo "Console: $url"; echo "Control token: $GRAPHX_CONTROL_TOKEN"
    ;;
  verify) load_state; verify ;;
  status) load_state; "${COMPOSE[@]}" ps; control status; sudo ovs-vsctl show ;;
  logs) load_state; "${COMPOSE[@]}" logs --tail=80; tail -n 40 "$GRAPHX_SDR_RUN_DIR/sdr.log" "$GRAPHX_SDR_RUN_DIR/tcpdump.log" ;;
  token) load_state; printf '%s\n' "$GRAPHX_CONTROL_TOKEN" ;;
  control) load_state; control "$@" ;;
  stop)
    if test ! -r "$state_file"; then echo "Already stopped; no external SDR state"; exit 0; fi
    load_state; cleanup; echo "Stopped; retained evidence: $GRAPHX_SDR_RUN_DIR"
    ;;
  *) usage; exit 64 ;;
esac

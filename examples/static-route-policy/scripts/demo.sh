#!/usr/bin/env bash
set -euo pipefail
umask 077

example_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
state_dir="$example_dir/.state"
state_file="$state_dir/lab.env"
graph_config="$example_dir/graphx.yaml"
diagnostic="$example_dir/tools/diagnostic.py"
project=gx-route-policy
GRAPHX_ROUTE_GUI_PORT=${GRAPHX_ROUTE_GUI_PORT:-8080}
url="http://127.0.0.1:$GRAPHX_ROUTE_GUI_PORT"
compose=(docker compose -p "$project" -f "$example_dir/compose.yaml")
owner_prefix=graphx-route

namespaces=(gx-route-router gx-route-left-end gx-route-middle-end gx-route-right-end)
bridges=(br-route-left br-route-middle br-route-right)
host_links=(rtl-ovs rtl-end rtl-cap-ovs rtl-cap rtm-ovs rtm-end rtm-cap-ovs rtm-cap
  rtr-ovs rtr-end rtr-cap-ovs rtr-cap rt-left-ovs rt-middle-ovs rt-right-ovs)

usage() {
  cat <<'EOF'
Usage: examples/static-route-policy/scripts/demo.sh <start|status|verify|apply-route|clear-route|logs|stop>

The native-Linux lab proves an allowed flow, an nftables policy denial, and a
missing route that becomes successful only after apply-route.
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

preflight_port() {
  python3 - "$GRAPHX_ROUTE_GUI_PORT" <<'PY'
import socket, sys
try:
    port = int(sys.argv[1])
    if not 1 <= port <= 65535: raise ValueError("outside 1..65535")
    with socket.socket() as endpoint: endpoint.bind(("127.0.0.1", port))
except (OSError, ValueError) as error:
    raise SystemExit(f"Route-policy GUI loopback port {sys.argv[1]} is unavailable: {error}")
PY
}

load_state() {
  test -r "$state_file" || { echo "No route-policy state; run start first" >&2; return 2; }
  # shellcheck disable=SC1090
  source "$state_file"
  case "$GRAPHX_ROUTE_RUN_DIR" in "$repo_dir"/outputs/static-route-policy/*) ;; *) echo "Unsafe route-policy run directory" >&2; return 2 ;; esac
  test -n "${GRAPHX_ROUTE_OWNER:-}" || { echo "Missing route-policy ownership token" >&2; return 2; }
  GRAPHX_ROUTE_GUI_PORT=${GRAPHX_ROUTE_GUI_PORT:-8080}
  url="http://127.0.0.1:$GRAPHX_ROUTE_GUI_PORT"
  export GRAPHX_ROUTE_RUN_DIR GRAPHX_ROUTE_GUI_PORT
}

write_evidence() {
  local routed_state=$1 routed_evidence=$2 route_applied=$3 temporary
  temporary="$GRAPHX_ROUTE_RUN_DIR/diagnostic-state.json.tmp.$$"
  python3 - "$temporary" "$routed_state" "$routed_evidence" "$route_applied" <<'PY'
import json, os, sys, time
path, routed_state, routed_evidence, applied = sys.argv[1:]
value = {"version": 1, "updatedAt": int(time.time() * 1000),
         "routeApplied": applied == "true", "flows": {
             "allowed-flow": {"state": "allowed", "evidence": "receiver-confirmed"},
             "denied-flow": {"state": "policy-denied", "evidence": "nft-counter"},
             "routed-flow": {"state": routed_state, "evidence": routed_evidence}}}
with open(path, "w", encoding="utf-8") as output:
    json.dump(value, output, separators=(",", ":"))
    output.write("\n")
os.chmod(path, 0o644)
PY
  mv "$temporary" "$GRAPHX_ROUTE_RUN_DIR/diagnostic-state.json"
}

create_state() {
  mkdir -p "$state_dir" "$repo_dir/outputs/static-route-policy"
  chmod 0700 "$state_dir"
  GRAPHX_ROUTE_RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
  GRAPHX_ROUTE_RUN_DIR="$repo_dir/outputs/static-route-policy/$GRAPHX_ROUTE_RUN_ID"
  GRAPHX_ROUTE_OWNER=$(openssl rand -hex 16)
  mkdir -p "$GRAPHX_ROUTE_RUN_DIR"
  chmod 0755 "$GRAPHX_ROUTE_RUN_DIR"
  local temporary="$state_file.tmp.$$"
  printf 'GRAPHX_ROUTE_RUN_ID=%q\nGRAPHX_ROUTE_RUN_DIR=%q\nGRAPHX_ROUTE_OWNER=%q\nGRAPHX_ROUTE_GUI_PORT=%q\n' \
    "$GRAPHX_ROUTE_RUN_ID" "$GRAPHX_ROUTE_RUN_DIR" "$GRAPHX_ROUTE_OWNER" \
    "$GRAPHX_ROUTE_GUI_PORT" >"$temporary"
  chmod 0600 "$temporary"
  mv "$temporary" "$state_file"
  export GRAPHX_ROUTE_RUN_DIR GRAPHX_ROUTE_GUI_PORT
  write_evidence missing-route route-absent false
}

native_resources_exist() {
  local name
  for name in "${host_links[@]}"; do
    sudo ip link show dev "$name" >/dev/null 2>&1 && return 0
  done
  for name in "${bridges[@]}"; do
    sudo ip link show dev "$name" >/dev/null 2>&1 && return 0
    sudo ovs-vsctl br-exists "$name" >/dev/null 2>&1 && return 0
  done
  for name in "${namespaces[@]}"; do
    sudo ip netns list | awk '{print $1}' | grep -qx "$name" && return 0
  done
  return 1
}

native_resources_owned() {
  local name value marker="$owner_prefix:$GRAPHX_ROUTE_OWNER"
  for name in "${bridges[@]}"; do
    if sudo ovs-vsctl br-exists "$name" >/dev/null 2>&1; then
      value=$(sudo ovs-vsctl get Bridge "$name" external_ids:graphx_route_owner | tr -d '"')
      test "$value" = "$GRAPHX_ROUTE_OWNER" || return 1
      value=$(sudo ovs-vsctl get Mirror "mirror-${name#br-}" \
        external_ids:graphx_route_owner | tr -d '"')
      test "$value" = "$GRAPHX_ROUTE_OWNER" || return 1
    fi
  done
  for name in "${host_links[@]}"; do
    if sudo ip link show dev "$name" >/dev/null 2>&1; then
      sudo ip -d link show dev "$name" | grep -Fq "alias $marker" || return 1
    fi
  done
  for name in "${namespaces[@]}"; do
    if sudo ip netns list | awk '{print $1}' | grep -qx "$name"; then
      sudo ip netns exec "$name" ip -d link show lo | grep -Fq "alias $marker" || return 1
    fi
  done
  return 0
}

preflight_names() {
  local name
  for name in "${host_links[@]}"; do
    if sudo ip link show dev "$name" >/dev/null 2>&1; then
      echo "Route-policy lab name is already occupied: $name" >&2; return 2
    fi
  done
  for name in "${bridges[@]}"; do
    if sudo ip link show dev "$name" >/dev/null 2>&1 ||
       sudo ovs-vsctl br-exists "$name" >/dev/null 2>&1; then
      echo "Route-policy OVS bridge name is already occupied: $name" >&2; return 2
    fi
  done
  for name in "${namespaces[@]}"; do
    if sudo ip netns list | awk '{print $1}' | grep -qx "$name"; then
      echo "Route-policy namespace is already occupied: $name" >&2; return 2
    fi
  done
  if sudo ip -j -4 route show table all | python3 -c '
import ipaddress, json, sys
targets = [ipaddress.ip_network(value) for value in
           ("10.64.1.0/24", "10.64.2.0/24", "10.64.3.0/24", "10.64.30.10/32")]
routes = json.load(sys.stdin)
raise SystemExit(0 if any(item.get("dst") not in (None, "default") and
    any(ipaddress.ip_network(item["dst"], strict=False).overlaps(target) for target in targets)
    for item in routes) else 1)
'; then
    echo "Route-policy address space is already present in the host routing tables" >&2
    return 2
  fi
}

mark_native_ownership() {
  local name marker="$owner_prefix:$GRAPHX_ROUTE_OWNER"
  for name in "${bridges[@]}"; do
    sudo ovs-vsctl set Bridge "$name" external_ids:graphx_route_owner="$GRAPHX_ROUTE_OWNER"
    sudo ovs-vsctl set Mirror "mirror-${name#br-}" external_ids:graphx_route_owner="$GRAPHX_ROUTE_OWNER"
  done
  for name in "${host_links[@]}"; do
    sudo ip link show dev "$name" >/dev/null 2>&1 && sudo ip link set dev "$name" alias "$marker"
  done
  sudo ip netns exec gx-route-router ip link set lo alias "$marker"
  for name in rt-left rt-middle rt-right; do
    sudo ip netns exec gx-route-router ip link set "$name" alias "$marker"
  done
}

create_endpoint() {
  local namespace=$1 peer=$2 address=$3 mac=$4 marker="$owner_prefix:$GRAPHX_ROUTE_OWNER"
  sudo ip netns add "$namespace"
  sudo ip link set "$peer" netns "$namespace"
  sudo ip netns exec "$namespace" ip link set lo up
  sudo ip netns exec "$namespace" ip link set lo alias "$marker"
  sudo ip netns exec "$namespace" ip link set "$peer" name eth0
  sudo ip netns exec "$namespace" ip link set eth0 address "$mac"
  sudo ip netns exec "$namespace" ip address add "$address" dev eth0
  sudo ip netns exec "$namespace" ip link set eth0 alias "$marker"
  sudo ip netns exec "$namespace" ip link set eth0 up
}

configure_endpoints() {
  create_endpoint gx-route-left-end rtl-end 10.64.1.10/24 02:64:00:00:01:10
  create_endpoint gx-route-middle-end rtm-end 10.64.2.10/24 02:64:00:00:02:10
  create_endpoint gx-route-right-end rtr-end 10.64.3.10/24 02:64:00:00:03:10
  sudo ip netns exec gx-route-left-end ip route add 10.64.2.0/24 via 10.64.1.1 dev eth0
  sudo ip netns exec gx-route-left-end ip route add 10.64.30.10/32 via 10.64.1.1 dev eth0
  sudo ip netns exec gx-route-middle-end ip route add 10.64.1.0/24 via 10.64.2.1 dev eth0
  sudo ip netns exec gx-route-right-end ip address add 10.64.30.10/32 dev lo
  sudo ip netns exec gx-route-right-end ip route add 10.64.1.0/24 via 10.64.3.1 dev eth0
}

owned_capture_pid() {
  local pid command
  test -r "$GRAPHX_ROUTE_RUN_DIR/capture.pid" || return 1
  pid=$(cat "$GRAPHX_ROUTE_RUN_DIR/capture.pid")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *dumpcap* ]] &&
    [[ "$command" == *rtl-cap* ]] && [[ "$command" == *rtm-cap* ]] &&
    [[ "$command" == *rtr-cap* ]]
}

start_capture() {
  sudo sh -c 'echo $$ > "$1"; chown "$2:$3" "$1"; chmod 0600 "$1"; exec dumpcap -q -i rtl-cap -i rtm-cap -i rtr-cap -s 65535 -a filesize:65536 -w - -f "udp port 18601 or udp port 18602 or udp port 18603"' \
    sh "$GRAPHX_ROUTE_RUN_DIR/capture.pid" "$(id -u)" "$(id -g)" \
    >"$GRAPHX_ROUTE_RUN_DIR/route-policy.pcapng" \
    2>"$GRAPHX_ROUTE_RUN_DIR/dumpcap.log" &
  for _ in {1..50}; do
    if owned_capture_pid && test -s "$GRAPHX_ROUTE_RUN_DIR/route-policy.pcapng"; then
      chmod 0644 "$GRAPHX_ROUTE_RUN_DIR/route-policy.pcapng"
      return
    fi
    sleep 0.1
  done
  echo "dumpcap failed to start on the three OVS mirror ports" >&2
  return 1
}

stop_capture() {
  owned_capture_pid || return 0
  local pid; pid=$(cat "$GRAPHX_ROUTE_RUN_DIR/capture.pid")
  sudo kill -INT "$pid" 2>/dev/null || true
  for _ in {1..50}; do kill -0 "$pid" 2>/dev/null || return 0; sleep 0.1; done
  sudo kill -KILL "$pid" 2>/dev/null || true
}

route_present() {
  sudo ip netns exec gx-route-router ip -j route show exact 10.64.30.10/32 | python3 -c '
import json, sys
routes = json.load(sys.stdin)
raise SystemExit(0 if len(routes) == 1 and routes[0].get("dst") == "10.64.30.10" and
    routes[0].get("gateway") == "10.64.3.10" and routes[0].get("dev") == "rt-right" else 1)
'
}

deny_packets() {
  sudo ip netns exec gx-route-router nft list chain inet graphx forward | \
    awk '/comment "deny-middle-left"/ { for (i=1; i<=NF; ++i) if ($i=="packets") { print $(i+1); exit } }'
}

probe() {
  local id=$1 sender_ns=$2 source=$3 receiver_ns=$4 destination=$5 port=$6
  local attempt token sender_output receiver_output
  attempt=$(date +%s%N)
  token="${GRAPHX_ROUTE_RUN_ID}-${id}-${attempt}"
  sender_output="$GRAPHX_ROUTE_RUN_DIR/$id-$attempt-sender.log"
  receiver_output="$GRAPHX_ROUTE_RUN_DIR/$id-$attempt-receiver.log"
  set +e
  sudo ip netns exec "$receiver_ns" python3 "$diagnostic" listen --bind "$destination" \
    --port "$port" --token "$token" --timeout 1.0 >"$receiver_output" 2>&1 &
  local listener=$!
  sleep 0.2
  sudo ip netns exec "$sender_ns" python3 "$diagnostic" send --bind "$source" \
    --destination "$destination" --port "$port" --token "$token" >"$sender_output" 2>&1
  local send_status=$?
  wait "$listener"
  local receive_status=$?
  set -e
  printf '%s %s\n' "$send_status" "$receive_status"
}

verify_flows() {
  local before after sent received
  read -r sent received < <(probe allowed-flow gx-route-left-end 10.64.1.10 \
    gx-route-middle-end 10.64.2.10 18601)
  test "$sent" -eq 0 && test "$received" -eq 0 || { echo "FAIL: intended flow did not arrive" >&2; return 1; }
  before=$(deny_packets); before=${before:-0}
  read -r sent received < <(probe denied-flow gx-route-middle-end 10.64.2.10 \
    gx-route-left-end 10.64.1.10 18602)
  after=$(deny_packets); after=${after:-0}
  test "$sent" -eq 0 && test "$received" -ne 0 && test "$after" -gt "$before" || {
    echo "FAIL: policy denial was not proven by receiver absence and nftables counter" >&2; return 1; }
  read -r sent received < <(probe routed-flow gx-route-left-end 10.64.1.10 \
    gx-route-right-end 10.64.30.10 18603)
  if route_present; then
    test "$sent" -eq 0 && test "$received" -eq 0 || { echo "FAIL: routed flow did not arrive after route apply" >&2; return 1; }
    write_evidence route-applied route-installed true
    echo "PASS: allowed flow arrived; denied flow incremented policy counter; routed flow arrived after route apply"
  else
    test "$sent" -eq 0 && test "$received" -ne 0 || {
      echo "FAIL: missing-route outcome requires sender success and receiver absence" >&2; return 1; }
    write_evidence missing-route route-absent false
    echo "PASS: allowed flow arrived; denied flow incremented policy counter; routed flow remained missing-route"
  fi
  curl -fsS "$url/api/topology" >/dev/null
}

cleanup() (
  set +e
  test -r "$state_file" || return 0
  load_state >/dev/null 2>&1 || return 2
  if native_resources_exist && ! native_resources_owned; then
    echo "Refusing to remove route-policy resources without matching ownership markers" >&2
    return 2
  fi
  "${compose[@]}" down --remove-orphans >/dev/null 2>&1
  stop_capture
  for namespace in gx-route-left-end gx-route-middle-end gx-route-right-end; do
    sudo ip netns delete "$namespace" >/dev/null 2>&1
  done
  local graphx; graphx=$(find_graphx 2>/dev/null) || graphx=
  test -z "$graphx" || sudo "$graphx" infra destroy "$graph_config" >/dev/null 2>&1
  return 0
)

rollback_start() {
  local status=$?
  echo "Route-policy startup failed; rolling back owned resources" >&2
  cleanup || true
  return "$status"
}

command_name=${1:-}; test -z "$command_name" || shift
case "$command_name" in
  start)
    test "$#" -eq 0 || { usage; exit 64; }
    test "$(uname -s)" = Linux || { echo "The route-policy laboratory requires native Linux" >&2; exit 2; }
    for tool in docker ovs-vsctl ip nft dumpcap sudo curl python3 openssl; do require "$tool"; done
    graphx=$(find_graphx)
    preflight_port
    sudo -v
    trap rollback_start EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    if test -r "$state_file"; then cleanup; fi
    preflight_names
    create_state
    sudo "$graphx" infra create "$graph_config" --transactional
    mark_native_ownership
    configure_endpoints
    for link in rtl-cap rtm-cap rtr-cap; do sudo ip link set "$link" up; done
    start_capture
    "${compose[@]}" up -d --build
    for _ in {1..60}; do curl -fsS "$url/api/ready" >/dev/null 2>&1 && break; sleep 1; done
    curl -fsS "$url/api/ready" >/dev/null
    verify_flows
    trap - EXIT INT TERM
    echo "Route-policy lab is running"
    echo "Console: $url"
    echo "Next: $0 apply-route"
    ;;
  verify)
    test "$#" -eq 0 || { usage; exit 64; }
    load_state; native_resources_owned || { echo "Route-policy ownership check failed" >&2; exit 2; }
    verify_flows
    ;;
  apply-route)
    test "$#" -eq 0 || { usage; exit 64; }
    load_state; graphx=$(find_graphx)
    native_resources_owned || { echo "Route-policy ownership check failed" >&2; exit 2; }
    sudo "$graphx" infra route apply "$graph_config" --router route-router --destination 10.64.30.10/32
    verify_flows
    ;;
  clear-route)
    test "$#" -eq 0 || { usage; exit 64; }
    load_state; graphx=$(find_graphx)
    native_resources_owned || { echo "Route-policy ownership check failed" >&2; exit 2; }
    sudo "$graphx" infra route clear "$graph_config" --router route-router --destination 10.64.30.10/32
    verify_flows
    ;;
  status)
    test "$#" -eq 0 || { usage; exit 64; }
    load_state; graphx=$(find_graphx)
    sudo "$graphx" infra status "$graph_config"
    "${compose[@]}" ps
    if route_present; then echo "Diagnostic route: applied"; else echo "Diagnostic route: absent"; fi
    python3 -m json.tool "$GRAPHX_ROUTE_RUN_DIR/diagnostic-state.json"
    ;;
  logs)
    test "$#" -eq 0 || { usage; exit 64; }
    load_state
    "${compose[@]}" logs --tail=80
    tail -n 40 "$GRAPHX_ROUTE_RUN_DIR"/*.log
    ;;
  stop)
    test "$#" -eq 0 || { usage; exit 64; }
    if test ! -r "$state_file"; then echo "Already stopped; no route-policy state"; exit 0; fi
    load_state
    cleanup
    echo "Stopped; retained evidence: $GRAPHX_ROUTE_RUN_DIR"
    ;;
  *) usage; exit 64 ;;
esac

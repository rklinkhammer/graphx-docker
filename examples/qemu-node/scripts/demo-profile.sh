#!/usr/bin/env bash
set -euo pipefail
umask 077

profile=${1:-}
command_name=${2:-}
if test -n "$profile"; then shift; fi
if test -n "$command_name"; then shift; fi
case "$profile" in external|container) ;; *) echo "profile must be external or container" >&2; exit 64 ;; esac

example_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
profile_dir="$example_dir/$profile"
state_dir="$example_dir/.state"
state_file="$state_dir/$profile.env"
requested_gui_port=${GRAPHX_QEMU_GUI_PORT:-8080}
GRAPHX_QEMU_GUI_PORT=$requested_gui_port
url=""
GRAPHX_HOST_UID=$(id -u)
GRAPHX_HOST_GID=$(id -g)
export GRAPHX_HOST_UID GRAPHX_HOST_GID
disable_capture=false
disable_history=false
requested_accel=auto

update_url() { url="http://127.0.0.1:$GRAPHX_QEMU_GUI_PORT"; }
update_url

usage() {
  cat <<EOF
Usage: examples/qemu-node/$profile/scripts/demo.sh <start|verify|status|logs|token|stop> [options]

  start    Build/start the $profile QEMU demo and verify live TCP/UDP traffic
  verify   Verify services, guest traffic, telemetry, capture, and packet history
  status   Show service, QEMU, guest, capture, and history status
  logs     Show recent service and guest output
  token    Print the browser control token
  stop     Stop owned resources; retained capture/history data is preserved

Start options:
  --accel auto|kvm|tcg|hvf  Select QEMU acceleration (hvf is external macOS only)
  --no-capture               Disable capture catalog output
  --no-history               Disable telemetry and packet history
EOF
}

require() { command -v "$1" >/dev/null 2>&1 || { echo "Missing prerequisite: $1" >&2; exit 2; }; }
preflight_ports() {
  python3 - "$profile" "${GRAPHX_QEMU_GUI_PORT:-8080}" <<'PY'
import socket
import sys
profile, gui = sys.argv[1], sys.argv[2]
if not gui.isdigit() or not 1 <= int(gui) <= 65535:
    raise SystemExit("GRAPHX_QEMU_GUI_PORT must be from 1 through 65535")
checks = [(socket.SOCK_STREAM, int(gui))]
if profile == "external":
    checks += [(socket.SOCK_STREAM, p) for p in (18001, 19001, 9100)]
    checks += [(socket.SOCK_DGRAM, p) for p in (9000, 18001, 19001)]
for kind, port in checks:
    with socket.socket(socket.AF_INET, kind) as candidate:
        try:
            if kind == socket.SOCK_STREAM:
                candidate.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            candidate.bind(("127.0.0.1", port))
            if kind == socket.SOCK_STREAM:
                candidate.listen(1)
        except OSError as error:
            protocol = "TCP" if kind == socket.SOCK_STREAM else "UDP"
            raise SystemExit(f"Required loopback port {port}/{protocol} is unavailable: {error}")
PY
}
hash_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  else shasum -a 256 "$1" | awk '{print $1}'; fi
}
valid_credential() {
  test "${#1}" -ge 32 && test "${#1}" -le 4096 || return 1
  case "$1" in *[![:graph:]]*) return 1 ;; esac
}
load_state() {
  test -r "$state_file" || { echo "The $profile demo has no state; run start first" >&2; return 2; }
  # The file is generated locally with fixed names and validated values.
  # shellcheck disable=SC1090
  source "$state_file"
  GRAPHX_QEMU_REQUESTED_ACCEL=${GRAPHX_QEMU_REQUESTED_ACCEL:-$GRAPHX_QEMU_ACCEL}
  GRAPHX_QEMU_GUI_PORT=${GRAPHX_QEMU_GUI_PORT:-8080}
  [[ "$GRAPHX_QEMU_GUI_PORT" =~ ^[0-9]+$ ]] &&
    test "$GRAPHX_QEMU_GUI_PORT" -ge 1 && test "$GRAPHX_QEMU_GUI_PORT" -le 65535 || {
      echo "Invalid QEMU GUI port in demo state" >&2; return 2;
    }
  update_url
  valid_credential "$GRAPHX_CONTROL_TOKEN" && valid_credential "$GRAPHX_TELEMETRY_SHARED_SECRET" || {
    echo "Invalid QEMU demo credential state" >&2; return 2;
  }
  case "$GRAPHX_QEMU_RUN_DIR" in "$repo_dir"/outputs/qemu-node/"$profile"/*) ;; *)
    echo "Unsafe QEMU run directory in state" >&2; return 2 ;; esac
  export GRAPHX_CONTROL_TOKEN GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_QEMU_RUN_ID GRAPHX_QEMU_RUN_DIR
  export GRAPHX_QEMU_ACCEL GRAPHX_QEMU_REQUESTED_ACCEL GRAPHX_QEMU_GUI_PORT
  export GRAPHX_CAPTURE_ENABLED GRAPHX_HISTORY_ENABLED
  if test "$GRAPHX_HISTORY_ENABLED" = true; then
    if test "$profile" = external; then GRAPHX_PACKET_HISTORY_URL=http://host.docker.internal:9100
    else GRAPHX_PACKET_HISTORY_URL=http://packet-observer:9100; fi
  else
    GRAPHX_PACKET_HISTORY_URL=
  fi
  export GRAPHX_PACKET_HISTORY_URL
}
create_state() {
  require openssl
  mkdir -p "$state_dir" "$repo_dir/outputs/qemu-node/$profile"
  chmod 0700 "$state_dir"
  local timestamp temporary
  timestamp=$(date -u +%Y%m%dT%H%M%SZ)
  GRAPHX_QEMU_RUN_ID=$timestamp
  GRAPHX_QEMU_RUN_DIR="$repo_dir/outputs/qemu-node/$profile/$timestamp"
  mkdir -p "$GRAPHX_QEMU_RUN_DIR"
  GRAPHX_CONTROL_TOKEN=$(openssl rand -hex 32)
  GRAPHX_TELEMETRY_SHARED_SECRET=$(openssl rand -hex 32)
  temporary="$state_file.tmp.$$"
  printf 'GRAPHX_CONTROL_TOKEN=%q\nGRAPHX_TELEMETRY_SHARED_SECRET=%q\nGRAPHX_QEMU_RUN_ID=%q\nGRAPHX_QEMU_RUN_DIR=%q\nGRAPHX_QEMU_ACCEL=%q\nGRAPHX_QEMU_REQUESTED_ACCEL=%q\nGRAPHX_QEMU_GUI_PORT=%q\nGRAPHX_CAPTURE_ENABLED=%q\nGRAPHX_HISTORY_ENABLED=%q\n' \
    "$GRAPHX_CONTROL_TOKEN" "$GRAPHX_TELEMETRY_SHARED_SECRET" "$GRAPHX_QEMU_RUN_ID" "$GRAPHX_QEMU_RUN_DIR" \
    "$GRAPHX_QEMU_ACCEL" "$GRAPHX_QEMU_REQUESTED_ACCEL" "$GRAPHX_QEMU_GUI_PORT" \
    "$GRAPHX_CAPTURE_ENABLED" "$GRAPHX_HISTORY_ENABLED" >"$temporary"
  chmod 0600 "$temporary"
  mv "$temporary" "$state_file"
  export GRAPHX_CONTROL_TOKEN GRAPHX_TELEMETRY_SHARED_SECRET GRAPHX_QEMU_RUN_ID GRAPHX_QEMU_RUN_DIR
  export GRAPHX_QEMU_ACCEL GRAPHX_QEMU_REQUESTED_ACCEL GRAPHX_QEMU_GUI_PORT
  export GRAPHX_CAPTURE_ENABLED GRAPHX_HISTORY_ENABLED
  if test "$GRAPHX_HISTORY_ENABLED" = true; then
    if test "$profile" = external; then GRAPHX_PACKET_HISTORY_URL=http://host.docker.internal:9100
    else GRAPHX_PACKET_HISTORY_URL=http://packet-observer:9100; fi
  else
    GRAPHX_PACKET_HISTORY_URL=
  fi
  export GRAPHX_PACKET_HISTORY_URL
}

select_accel() {
  local kernel machine
  kernel=$(uname -s)
  machine=$(uname -m)
  if test "$profile" = container && test "$kernel" != Linux; then
    echo "The containerized QEMU demo is supported only on native Linux" >&2
    return 2
  fi
  case "$requested_accel" in
    auto)
      if test "$kernel" = Linux && test -c /dev/kvm && test -r /dev/kvm && test -w /dev/kvm; then
        GRAPHX_QEMU_ACCEL=kvm
      elif test "$profile" = external && test "$kernel" = Darwin && test "$machine" = x86_64; then
        GRAPHX_QEMU_ACCEL=hvf
      else
        GRAPHX_QEMU_ACCEL=tcg
      fi
      ;;
    kvm)
      test "$kernel" = Linux && test -c /dev/kvm && test -r /dev/kvm && test -w /dev/kvm || {
        echo "KVM requested but /dev/kvm is not accessible" >&2; return 2; }
      GRAPHX_QEMU_ACCEL=kvm
      ;;
    hvf)
      test "$profile" = external && test "$kernel" = Darwin && test "$machine" = x86_64 || {
        echo "HVF for this x86_64 guest requires the external demo on an Intel Mac" >&2; return 2; }
      GRAPHX_QEMU_ACCEL=hvf
      ;;
    tcg) GRAPHX_QEMU_ACCEL=tcg ;;
    *) echo "--accel must be auto, kvm, tcg, or hvf" >&2; return 64 ;;
  esac
  GRAPHX_QEMU_REQUESTED_ACCEL=$requested_accel
  export GRAPHX_QEMU_REQUESTED_ACCEL
}

compose_files() {
  COMPOSE=(docker compose -f "$profile_dir/compose.yaml")
  if test "$GRAPHX_HISTORY_ENABLED" = true; then COMPOSE+=(-f "$profile_dir/compose.history.yaml"); fi
  if test "$profile" = container && test "$GRAPHX_QEMU_ACCEL" = kvm; then
    GRAPHX_KVM_GID=$(stat -c %g /dev/kvm)
    export GRAPHX_KVM_GID
    COMPOSE+=(-f "$profile_dir/compose.kvm.yaml")
  fi
}

owned_external_pid() {
  local pid command
  test -r "$GRAPHX_QEMU_RUN_DIR/qemu.pid" || return 1
  pid=$(cat "$GRAPHX_QEMU_RUN_DIR/qemu.pid")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *qemu-system-x86_64* ]] &&
    [[ "$command" == *"$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap"* ]]
}

owned_observer_pid() {
  local pid command
  test -r "$GRAPHX_QEMU_RUN_DIR/observer.pid" || return 1
  pid=$(cat "$GRAPHX_QEMU_RUN_DIR/observer.pid")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *packet_observer.py* ]] &&
    [[ "$command" == *"$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap"* ]]
}

owned_readiness_pid() {
  local pid command
  test -r "$GRAPHX_QEMU_RUN_DIR/readiness.pid" || return 1
  pid=$(cat "$GRAPHX_QEMU_RUN_DIR/readiness.pid")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *qmp_control.py* ]] &&
    [[ "$command" == *guest-readiness* ]] &&
    [[ "$command" == *"$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json"* ]]
}

start_external_readiness() {
  python3 "$example_dir/tools/qmp_control.py" guest-readiness \
    --target 127.0.0.1 --port 18001 \
    --socket "$state_dir/external.qmp" \
    --output "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" \
    --wait 60 --interval 2 --failure-threshold 3 --monitor \
    --daemonize --pid-file "$GRAPHX_QEMU_RUN_DIR/readiness.pid" \
    --log-file "$GRAPHX_QEMU_RUN_DIR/probe.log"
  for _ in {1..20}; do
    test -s "$GRAPHX_QEMU_RUN_DIR/readiness.pid" && break
    sleep 0.05
  done
  owned_readiness_pid || return 1
  for _ in {1..300}; do
    if python3 - "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" <<'PY'
import json, sys
try:
    value = json.load(open(sys.argv[1], encoding="utf-8"))
except (OSError, ValueError):
    raise SystemExit(1)
raise SystemExit(0 if value.get("state") == "ready" and
                 value.get("guestProtocols") == {"tcp": True, "udp": True} else 1)
PY
    then return 0; fi
    owned_readiness_pid || return 1
    sleep 0.2
  done
  return 1
}

start_external_observer() {
  local bind_address=127.0.0.1
  if test "$(uname -s)" = Linux; then bind_address=172.30.12.1; fi
  env \
    GRAPHX_QEMU_RUN_ID="$GRAPHX_QEMU_RUN_ID" \
    GRAPHX_TELEMETRY_HOST=127.0.0.1 GRAPHX_TELEMETRY_PORT=9000 \
    GRAPHX_TELEMETRY_SHARED_SECRET="$GRAPHX_TELEMETRY_SHARED_SECRET" \
    GRAPHX_CAPTURE_ENABLED="$GRAPHX_CAPTURE_ENABLED" \
    GRAPHX_PACKET_HISTORY_ENABLED="$GRAPHX_HISTORY_ENABLED" \
    python3 "$example_dir/tools/packet_observer.py" \
      --capture "$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap" \
      --pcapng "$GRAPHX_QEMU_RUN_DIR/qemu-node.pcapng" \
      --database "$GRAPHX_QEMU_RUN_DIR/packet-history.sqlite" \
      --http-bind "$bind_address" --daemonize \
      --pid-file "$GRAPHX_QEMU_RUN_DIR/observer.pid" \
      --log-file "$GRAPHX_QEMU_RUN_DIR/observer.log"
  for _ in {1..30}; do
    curl -fsS "http://$bind_address:9100/health" >/dev/null 2>&1 && return 0
    if test -e "$GRAPHX_QEMU_RUN_DIR/observer.pid" && ! owned_observer_pid; then return 1; fi
    sleep 0.2
  done
  return 1
}

start_external_qemu() {
  require qemu-system-x86_64
  local machine cpu
  case "$GRAPHX_QEMU_ACCEL" in
    kvm) machine=kvm; cpu=host ;;
    hvf) machine=hvf; cpu=host ;;
    tcg) machine=tcg; cpu=qemu64 ;;
  esac
  rm -f "$state_dir/external.qmp"
  qemu-system-x86_64 \
    -machine "q35,accel=$machine" -cpu "$cpu" -m 256M -smp 1 \
    -kernel "$example_dir/output/images/bzImage" \
    -initrd "$example_dir/output/images/rootfs.cpio.gz" \
    -append "console=ttyS0 panic=1" -no-reboot -display none -monitor none \
    -serial "file:$GRAPHX_QEMU_RUN_DIR/guest-console.log" \
    -daemonize -pidfile "$GRAPHX_QEMU_RUN_DIR/qemu.pid" \
    -qmp "unix:$state_dir/external.qmp,server=on,wait=off" \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:18001-:18001,hostfwd=udp:127.0.0.1:18001-:18001" \
    -device virtio-net-pci,netdev=net0 \
    -object "filter-dump,id=capture0,netdev=net0,file=$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap"
  (
    local pid bytes
    pid=$(cat "$GRAPHX_QEMU_RUN_DIR/qemu.pid")
    while kill -0 "$pid" 2>/dev/null; do
      command=$(ps -p "$pid" -o command= 2>/dev/null || true)
      if [[ "$command" != *qemu-system-x86_64* ]] ||
          [[ "$command" != *"$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap"* ]]; then
        break
      fi
      if test -f "$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap"; then
        bytes=$(wc -c <"$GRAPHX_QEMU_RUN_DIR/qemu-node.pcap")
        if test "$bytes" -ge "${GRAPHX_CAPTURE_SOURCE_MAX_BYTES:-67108864}"; then
          echo "QEMU source capture reached its bounded limit" >>"$GRAPHX_QEMU_RUN_DIR/guest-console.log"
          kill "$pid" 2>/dev/null || true
          break
        fi
      fi
      sleep 1
    done
    python3 "$example_dir/tools/qmp_control.py" state \
      --output "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" --state degraded \
      --reason "external QEMU process exited" 2>/dev/null || true
  ) &
  echo "$!" >"$GRAPHX_QEMU_RUN_DIR/capture-guard.pid"
}

probe_qemu() {
  local socket_path
  if test "$profile" = external; then socket_path="$state_dir/external.qmp"
  else return 0; fi
  python3 "$example_dir/tools/qmp_control.py" probe --socket "$socket_path" \
    --output "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" \
    --requested "$GRAPHX_QEMU_REQUESTED_ACCEL" --selected "$GRAPHX_QEMU_ACCEL"
}

wait_http() {
  printf 'Waiting for telemetry'
  for _ in {1..90}; do
    if curl -fsS "$url/api/health" >/dev/null 2>&1; then printf ' ready\n'; return 0; fi
    printf '.'; sleep 1
  done
  printf ' timed out\n' >&2
  return 1
}

packet_total() {
  python3 -c 'import json,sys; d=json.load(sys.stdin); print(sum(v.get("received",0) for v in d.get("edges",{}).values()))'
}

verify_demo() {
  require curl
  require python3
  wait_http
  local first second snapshot running container_id
  running=$("${COMPOSE[@]}" ps --status running --services)
  local services=(host-origin host-receiver telemetry)
  if test "$profile" = container; then services+=(packet-observer); fi
  for service in "${services[@]}"; do
    grep -qx "$service" <<<"$running" || { echo "FAIL: $service is not running" >&2; return 1; }
  done
  if test "$profile" = container; then
    grep -qx qemu-node <<<"$running" || { echo "FAIL: qemu-node is not running" >&2; return 1; }
    container_id=$("${COMPOSE[@]}" ps -q qemu-node)
    test -n "$container_id" && test "$(docker inspect --format '{{.State.Health.Status}}' "$container_id")" = healthy || {
      echo "FAIL: qemu guest application is not healthy" >&2; return 1; }
  else
    owned_external_pid || { echo "FAIL: owned external QEMU process is not running" >&2; return 1; }
    owned_observer_pid || { echo "FAIL: owned host packet observer is not running" >&2; return 1; }
  fi
  snapshot=$(curl -fsS "$url/api/topology")
  GRAPHX_QEMU_SNAPSHOT="$snapshot" python3 - "$GRAPHX_QEMU_ACCEL" <<'PY'
import json, os, sys
value = json.loads(os.environ["GRAPHX_QEMU_SNAPSHOT"])
qemu = next(node for node in value["topology"]["nodes"] if node["id"] == "qemu-node")
expected = sys.argv[1]
assert qemu.get("actualAccelerator") == expected, (qemu, expected)
assert value["nodes"]["qemu-node"]["status"] == "ready"
assert qemu.get("vmState") == "running"
assert qemu.get("guestState") == "ready"
assert qemu.get("guestProtocols") == {"tcp": True, "udp": True}
if expected == "kvm":
    assert qemu.get("acceleratorEvidence") == "QMP query-status + query-kvm"
PY
  first=$(packet_total <<<"$snapshot")
  second=$first
  for _ in {1..35}; do
    sleep 1
    snapshot=$(curl -fsS "$url/api/topology")
    second=$(packet_total <<<"$snapshot")
    test "$second" -gt "$first" && break
  done
  test "$second" -gt "$first" || { echo "FAIL: packet counters did not advance ($first -> $second)" >&2; return 1; }
  if test "$GRAPHX_CAPTURE_ENABLED" = true; then
    python3 -c 'import json,sys; d=json.load(sys.stdin); assert d["capture"]["enabled"] and any(f["format"] == "ethernet" for f in d["capture"]["files"])' <<<"$snapshot"
    echo "PASS: bounded Ethernet PCAPNG is cataloged"
  else
    python3 -c 'import json,sys; assert not json.load(sys.stdin)["capture"]["enabled"]' <<<"$snapshot"
    echo "PASS: cataloged capture is disabled as requested"
  fi
  if test "$GRAPHX_HISTORY_ENABLED" = true; then
    curl -fsS "$url/api/packet-history?limit=1" | python3 -c 'import json,sys; assert json.load(sys.stdin)["records"]'
    echo "PASS: bounded packet history is queryable"
  else
    test "$(curl -sS -o /dev/null -w '%{http_code}' "$url/api/packet-history?limit=1")" = 503 || {
      echo "FAIL: disabled packet history endpoint did not return 503" >&2; return 1; }
    echo "PASS: packet history is disabled as requested"
  fi
  echo "PASS: $profile services and guest application are running"
  echo "PASS: raw TCP/UDP packet counters advanced $first -> $second"
  echo "PASS: GUI and telemetry API are available at $url"
}

start_demo() {
  require python3
  if test "$profile" = container && test "$(uname -s)" != Linux; then
    echo "The containerized QEMU demo is supported only on native Linux" >&2
    return 2
  fi
  require docker; require curl
  docker info >/dev/null 2>&1 || { echo "Docker is not available" >&2; return 2; }
  if test -e "$state_file"; then
    load_state || { echo "Move the invalid state file aside only after confirming no demo resources remain" >&2; return 2; }
    compose_files
    if owned_external_pid 2>/dev/null || owned_observer_pid 2>/dev/null || test -n "$("${COMPOSE[@]}" ps -q 2>/dev/null)"; then
      echo "The $profile demo is already running or was not stopped; run its stop command first" >&2
      return 2
    fi
  fi
  GRAPHX_QEMU_GUI_PORT=$requested_gui_port
  export GRAPHX_QEMU_GUI_PORT
  update_url
  preflight_ports
  for image in bzImage rootfs.cpio.gz; do
    test -s "$example_dir/output/images/$image" || { echo "Missing guest image; run $example_dir/scripts/build.sh" >&2; return 2; }
  done
  python3 "$example_dir/tools/artifact_manifest.py" verify --images "$example_dir/output/images" \
    --guest "$example_dir/guest" || return 2
  select_accel
  GRAPHX_CAPTURE_ENABLED=true
  GRAPHX_HISTORY_ENABLED=true
  test "$disable_capture" = false || GRAPHX_CAPTURE_ENABLED=false
  test "$disable_history" = false || GRAPHX_HISTORY_ENABLED=false
  create_state
  cp "$example_dir/output/images/manifest.json" "$GRAPHX_QEMU_RUN_DIR/guest-manifest.json"
  chmod 0644 "$GRAPHX_QEMU_RUN_DIR/guest-manifest.json"
  python3 "$example_dir/tools/qmp_control.py" state \
    --output "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" --state booting
  compose_files
  source "$repo_dir/scripts/configure-build-trust.sh"
  if test "$profile" = external; then
    if ! "${COMPOSE[@]}" up -d --build host-receiver telemetry; then stop_demo; return 1; fi
    start_external_observer || { echo "Host packet observer did not become ready" >&2; stop_demo; return 1; }
    start_external_qemu || { echo "External QEMU did not start" >&2; stop_demo; return 1; }
    probe_qemu || { echo "QMP accelerator verification failed" >&2; stop_demo; return 1; }
    start_external_readiness || { echo "Guest did not become ready on both TCP and UDP" >&2; logs_demo; stop_demo; return 1; }
    "${COMPOSE[@]}" up -d host-origin
  else
    if ! "${COMPOSE[@]}" up -d --build; then stop_demo; return 1; fi
  fi
  verify_demo
  printf '\nProfile: %s · platform: %s/%s\n' "$profile" "$(uname -s)" "$(uname -m)"
  python3 - "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
print("Accelerator: requested={requestedAccelerator} selected={selectedAccelerator} actual={actualAccelerator} · evidence={evidenceSource}".format(**value))
PY
  printf 'Guest bzImage SHA-256: %s\nGuest rootfs SHA-256: %s\nGuest manifest SHA-256: %s\n' \
    "$(hash_file "$example_dir/output/images/bzImage")" \
    "$(hash_file "$example_dir/output/images/rootfs.cpio.gz")" \
    "$(hash_file "$example_dir/output/images/manifest.json")"
  printf 'Capture: %s (64 MiB/100,000 packets) · history: %s (1 day/50,000 records/64 MiB)\nConsole/API: %s\nControl token: %s\nRetrieve later: %s/scripts/demo.sh token\nArtifacts: %s\n' \
    "$GRAPHX_CAPTURE_ENABLED" "$GRAPHX_HISTORY_ENABLED" "$url" "$GRAPHX_CONTROL_TOKEN" "$profile_dir" "$GRAPHX_QEMU_RUN_DIR"
}

status_demo() {
  local snapshot
  "${COMPOSE[@]}" ps
  if test "$profile" = external; then
    if owned_external_pid; then echo "External QEMU: running (pid $(cat "$GRAPHX_QEMU_RUN_DIR/qemu.pid"))"
    else echo "External QEMU: stopped"; fi
  fi
  snapshot=$(curl -fsS "$url/api/topology" 2>/dev/null) || {
    echo "Telemetry API is unavailable at $url; inspect logs with demo.sh logs" >&2
    return 1
  }
  GRAPHX_QEMU_SNAPSHOT="$snapshot" python3 - <<'PY'
import json, os, sys
try:
    value = json.loads(os.environ["GRAPHX_QEMU_SNAPSHOT"])
    qemu = next(node for node in value["topology"]["nodes"] if node["id"] == "qemu-node")
    print("Graph:", value["graph"], "guest:", value["nodes"]["qemu-node"]["status"],
          "VM:", qemu.get("vmState", "unknown"),
          "guest application:", qemu.get("guestState", "unknown"),
          "accelerator: requested", qemu.get("requestedAccelerator", "unknown"),
          "selected", qemu.get("selectedAccelerator", "unknown"),
          "actual", qemu.get("actualAccelerator", "not proven"),
          "capture:", "ready" if value["capture"]["enabled"] else "disabled",
          "packet history:", value.get("packetHistory", {}).get("status", "unknown"))
except (KeyError, TypeError, ValueError, StopIteration) as error:
    print(f"Telemetry API returned an invalid topology response: {error}", file=sys.stderr)
    raise SystemExit(1)
PY
}

logs_demo() {
  local services=(host-origin host-receiver telemetry)
  if test "$profile" = container; then services+=(packet-observer qemu-node); fi
  "${COMPOSE[@]}" logs --tail=80 "${services[@]}" 2>/dev/null || true
  if test "$profile" = external && test -r "$GRAPHX_QEMU_RUN_DIR/guest-console.log"; then
    echo "==> external QEMU guest"; tail -80 "$GRAPHX_QEMU_RUN_DIR/guest-console.log"
  fi
  if test "$profile" = external && test -r "$GRAPHX_QEMU_RUN_DIR/observer.log"; then
    echo "==> host packet observer"; tail -80 "$GRAPHX_QEMU_RUN_DIR/observer.log"
  fi
}

stop_demo() {
  if test "$profile" = external; then "${COMPOSE[@]}" stop -t 2 host-origin >/dev/null 2>&1 || true; fi
  if test "$profile" = external && owned_external_pid; then
    local pid
    pid=$(cat "$GRAPHX_QEMU_RUN_DIR/qemu.pid")
    python3 "$example_dir/tools/qmp_control.py" shutdown \
      --socket "$state_dir/external.qmp" --wait 3 >/dev/null 2>&1 || true
    for _ in {1..10}; do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
  fi
  if test "$profile" = external && owned_readiness_pid; then
    local readiness_pid
    readiness_pid=$(cat "$GRAPHX_QEMU_RUN_DIR/readiness.pid")
    kill "$readiness_pid" 2>/dev/null || true
    for _ in {1..20}; do kill -0 "$readiness_pid" 2>/dev/null || break; sleep 0.1; done
  fi
  if test "$profile" = external && owned_observer_pid; then
    sleep 1
    local observer_pid
    observer_pid=$(cat "$GRAPHX_QEMU_RUN_DIR/observer.pid")
    kill "$observer_pid" 2>/dev/null || true
    for _ in {1..10}; do kill -0 "$observer_pid" 2>/dev/null || break; sleep 0.2; done
    if kill -0 "$observer_pid" 2>/dev/null; then kill -KILL "$observer_pid" 2>/dev/null || true; fi
  fi
  "${COMPOSE[@]}" down --remove-orphans
  if test "$profile" = external; then
    local guard_pid
    if test -r "$GRAPHX_QEMU_RUN_DIR/capture-guard.pid"; then
      guard_pid=$(cat "$GRAPHX_QEMU_RUN_DIR/capture-guard.pid")
      if [[ "$guard_pid" =~ ^[1-9][0-9]*$ ]]; then
        for _ in {1..20}; do kill -0 "$guard_pid" 2>/dev/null || break; sleep 0.1; done
      fi
    fi
    python3 "$example_dir/tools/qmp_control.py" state \
      --output "$GRAPHX_QEMU_RUN_DIR/accelerator-evidence.json" --state stopped 2>/dev/null || true
    rm -f "$state_dir/external.qmp" "$GRAPHX_QEMU_RUN_DIR/qemu.pid" \
      "$GRAPHX_QEMU_RUN_DIR/observer.pid" "$GRAPHX_QEMU_RUN_DIR/readiness.pid" \
      "$GRAPHX_QEMU_RUN_DIR/capture-guard.pid"
  fi
  echo "Stopped $profile QEMU demo; retained artifacts/history were not deleted"
}

while test "$#" -gt 0; do
  case "$1" in
    --accel) requested_accel=${2:-}; shift 2 ;;
    --no-capture) disable_capture=true; shift ;;
    --no-history) disable_history=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) usage; exit 64 ;;
  esac
done

case "$command_name" in
  start) start_demo ;;
  verify|status|logs|token|stop)
    load_state
    compose_files
    case "$command_name" in
      verify) verify_demo ;;
      status) status_demo ;;
      logs) logs_demo ;;
      token) printf '%s\n' "$GRAPHX_CONTROL_TOKEN" ;;
      stop) stop_demo ;;
    esac
    ;;
  *) usage; exit 64 ;;
esac

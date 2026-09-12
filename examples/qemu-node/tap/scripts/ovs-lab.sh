#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

profile_dir=$(cd "$(dirname "$0")/.." && pwd)
example_dir=$(cd "$profile_dir/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
graphx=${GRAPHX_BIN:-$repo_dir/build/dev/graphx}
config=$profile_dir/graphx.yaml
images=$example_dir/output/images
run_dir=${GRAPHX_QEMU_RUN_DIR:-/var/lib/graphx/qemu/runtime}
qemu_uid=65532
qemu_gid=65532
qmp_source=$example_dir/tools/qmp_control.py
peer_source=$example_dir/host/peer.py
observer_source=$example_dir/tools/packet_observer.py
exporter_source=$example_dir/tools/capture_exporter.sh
runtime_images=$run_dir/images
qmp=$run_dir/qmp_control.py
peer=$run_dir/peer.py
observer=$run_dir/packet_observer.py
capture_handoff=${GRAPHX_QEMU_CAPTURE_HANDOFF_DIR:-/var/lib/graphx/qemu-capture-handoff}
capture_exporter=$capture_handoff/capture_exporter.sh
qemu_args=(
  -machine q35,accel=tcg -cpu qemu64 -m 256M -smp 1
  -kernel "$runtime_images/bzImage" -initrd "$runtime_images/rootfs.cpio.gz"
  -append "console=ttyS0 panic=1" -no-reboot -display none -monitor none
  -serial "file:$run_dir/guest-console.log" -daemonize
  -pidfile "$run_dir/qemu.pid" -qmp "unix:$run_dir/qemu.qmp,server=on,wait=off"
  -netdev tap,id=net0,ifname=gxqtap0,script=no,downscript=no
  -device virtio-net-pci,netdev=net0,mac=02:00:00:00:02:15
)

require() { command -v "$1" >/dev/null || { echo "missing required command: $1" >&2; exit 1; }; }
owned_pid() {
  local file=$1 marker=$2 pid command
  [[ "$file" = /* ]] || file=$run_dir/$file
  pid=$(sudo cat "$file" 2>/dev/null || true)
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(sudo ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *"$marker"* ]]
}
stop_owned() {
  local file=$1 marker=$2 pid command
  [[ "$file" = /* ]] || file=$run_dir/$file
  sudo test -e "$file" || return 0
  pid=$(sudo cat "$file")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || {
    echo "refusing malformed $file process identity" >&2; return 1;
  }
  if ! sudo kill -0 "$pid" 2>/dev/null; then
    sudo rm -f "$file"
    return 0
  fi
  command=$(sudo ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *"$marker"* ]] || {
    echo "refusing to signal PID $pid: expected $marker identity" >&2; return 1;
  }
  sudo kill "$pid" 2>/dev/null || true
  for _ in {1..30}; do sudo kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
  sudo kill -0 "$pid" 2>/dev/null && sudo kill -KILL "$pid" 2>/dev/null || true
  sudo rm -f "$file"
}
qemu_identity() {
  local uid_name gid_name
  uid_name=$(getent passwd "$qemu_uid" | cut -d: -f1 || true)
  gid_name=$(getent group "$qemu_gid" | cut -d: -f1 || true)
  test "$uid_name" = graphx-qemu && test "$gid_name" = graphx-qemu || {
    echo "UID/GID 65532 must belong to graphx-qemu; provision the native Linux identity or rerun Lima provisioning" >&2
    return 1
  }
}
qmp_as_qemu() {
  sudo setpriv --reuid "$qemu_uid" --regid "$qemu_gid" --clear-groups \
    python3 "$qmp" "$@"
}
qmp_in_peer() {
  sudo ip netns exec gx-qemu-peer setpriv --reuid "$qemu_uid" \
    --regid "$qemu_gid" --clear-groups python3 "$qmp" "$@"
}
prepare_run_dir() {
  local normalized
  normalized=$(mktemp)
  GRAPHX_OVERRIDES= "$graphx" config normalize "$config" >"$normalized"
  sudo install -d -o "$qemu_uid" -g "$qemu_gid" -m 0750 "$run_dir"
  sudo install -d -o "$qemu_uid" -g "$qemu_gid" -m 0750 "$runtime_images"
  sudo install -o "$qemu_uid" -g "$qemu_gid" -m 0550 -t "$run_dir" \
    "$qmp_source" "$peer_source" "$observer_source"
  sudo install -d -o root -g "$qemu_gid" -m 0750 "$capture_handoff"
  sudo install -o root -g root -m 0550 "$exporter_source" "$capture_exporter"
  sudo install -o "$qemu_uid" -g "$qemu_gid" -m 0440 "$normalized" "$run_dir/normalized.json"
  rm -f "$normalized"
  sudo install -o "$qemu_uid" -g "$qemu_gid" -m 0440 -t "$runtime_images" \
    "$images/bzImage" "$images/rootfs.cpio.gz"
  sudo rm -f "$run_dir/qemu.qmp" "$run_dir/qemu.pid" "$run_dir/peer.pid" \
    "$run_dir/observer.pid"
  sudo rm -f "$run_dir/packet-history.sqlite" "$run_dir/packet-history.sqlite-wal" \
    "$run_dir/packet-history.sqlite-shm"
  sudo rm -f "$run_dir/accelerator-evidence.json" "$run_dir/guest-console.log" \
    "$run_dir/observer.log" "$run_dir/peer.log"
  sudo rm -f "$capture_handoff/exporter.pid" "$capture_handoff/qemu-span.pcapng" \
    "$capture_handoff"/.qemu-span.*.pcapng
}
start_peer() {
  # shellcheck disable=SC2024
  sudo sh -c 'echo $$ >"$1"; exec ip netns exec gx-qemu-peer python3 "$2" --receiver --bind 10.0.2.2' \
    sh "$run_dir/peer.pid" "$peer" >"/tmp/graphx-qemu-tap-peer.$$.log" 2>&1 &
  for _ in {1..30}; do owned_pid peer.pid peer.py && break; sleep 0.1; done
  owned_pid peer.pid peer.py || { echo "host peer failed to start" >&2; return 1; }
  sudo mv "/tmp/graphx-qemu-tap-peer.$$.log" "$run_dir/peer.log"
}
start_capture() {
  sudo "$capture_exporter" "$capture_handoff/exporter.pid" "$graphx" "$config" \
    qemu-span "$capture_handoff" "$qemu_gid" >/dev/null 2>&1 &
  for _ in {1..100}; do
    if owned_pid "$capture_handoff/exporter.pid" capture_exporter.sh &&
       sudo test -s "$capture_handoff/qemu-span.pcapng"; then
      break
    fi
    sleep 0.1
  done
  owned_pid "$capture_handoff/exporter.pid" capture_exporter.sh || {
    echo "managed capture exporter failed to start" >&2
    return 1
  }
  sudo test -s "$capture_handoff/qemu-span.pcapng" || {
    echo "managed capture did not publish a bounded snapshot" >&2
    return 1
  }
  sudo setpriv --reuid "$qemu_uid" --regid "$qemu_gid" --clear-groups \
    env GRAPHX_QEMU_RUN_ID=m6-tap GRAPHX_PACKET_OBSERVATION_SOURCE=ovs-span \
    GRAPHX_NETWORK_CAPTURE_ID=qemu-span \
    GRAPHX_NORMALIZED_CONFIG="$run_dir/normalized.json" \
    python3 "$observer" --capture "$capture_handoff/qemu-span.pcapng" \
      --database "$run_dir/packet-history.sqlite" \
      --http-port 9106 --daemonize --pid-file "$run_dir/observer.pid" \
      --log-file "$run_dir/observer.log"
}
start_qemu() {
  sudo setpriv --reuid "$qemu_uid" --regid "$qemu_gid" --clear-groups \
    qemu-system-x86_64 "${qemu_args[@]}"
  for _ in {1..50}; do owned_pid qemu.pid qemu-system-x86_64 && break; sleep 0.1; done
  owned_pid qemu.pid qemu-system-x86_64 || { echo "QEMU failed to start" >&2; return 1; }
  qmp_as_qemu probe --socket "$run_dir/qemu.qmp" \
    --output "$run_dir/accelerator-evidence.json" --requested tcg --selected tcg
  qmp_in_peer guest-readiness --target 10.0.2.15 --port 18001 \
    --socket "$run_dir/qemu.qmp" --output "$run_dir/accelerator-evidence.json" \
    --wait 60 --interval 1
}
stop_qemu() {
  if owned_pid qemu.pid qemu-system-x86_64; then
    qmp_as_qemu shutdown --socket "$run_dir/qemu.qmp" --wait 3 >/dev/null 2>&1 || true
  fi
  stop_owned qemu.pid qemu-system-x86_64
  sudo rm -f "$run_dir/qemu.qmp"
}
stop_runtime() {
  stop_qemu
  stop_owned observer.pid packet_observer.py
  stop_owned "$capture_handoff/exporter.pid" capture_exporter.sh
  stop_owned peer.pid peer.py
}
rollback_up() {
  local original=$? runtime_status=0
  trap - ERR
  set +e
  stop_runtime || runtime_status=$?
  if test "$runtime_status" -eq 0; then
    sudo "$graphx" infra destroy "$config"
  else
    echo "rollback preserved QEMU TAP infrastructure because a process identity changed" >&2
  fi
  exit "$original"
}
verify_network() {
  local before after tap_before tap_after pid process_uid process_gid
  sudo "$graphx" infra status "$config"
  pid=$(sudo cat "$run_dir/qemu.pid")
  read -r process_uid process_gid < <(sudo ps -p "$pid" -o uid=,gid=)
  test "$process_uid" = "$qemu_uid" && test "$process_gid" = "$qemu_gid"
  sudo ip tuntap show dev gxqtap0 | grep -q "user $qemu_uid group $qemu_gid"
  qmp_as_qemu probe --socket "$run_dir/qemu.qmp" \
    --output "$run_dir/accelerator-evidence.json" --requested tcg --selected tcg >/dev/null
  qmp_in_peer guest-readiness --target 10.0.2.15 --port 18001 \
    --socket "$run_dir/qemu.qmp" --output "$run_dir/accelerator-evidence.json" \
    --wait 10 --interval 1
  sudo ovs-appctl fdb/show br-qemu-tap | grep -qi '02:00:00:00:02:15'
  test "$(sudo ovs-vsctl get Port gxqtap0 tag)" = 42
  test "$(sudo ovs-vsctl get Port gxqpeer0 tag)" = 42
  test "$(sudo ovs-vsctl get Port gxqiso0 tag)" = 43
  before=$(curl -fsS http://127.0.0.1:9106/status | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["capturePackets"])')
  tap_before=$(sudo cat /sys/class/net/gxqtap0/statistics/tx_packets)
  sudo ip netns exec gx-qemu-peer python3 -c \
    'import socket; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(("10.0.2.2",0)); s.settimeout(2); s.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1); payload=b"graphx-broadcast"; s.sendto(payload,("10.0.2.255",18001)); reply,_=s.recvfrom(1024); assert reply==payload; s.setsockopt(socket.IPPROTO_IP,socket.IP_MULTICAST_IF,socket.inet_aton("10.0.2.2")); s.sendto(b"graphx-multicast",("239.1.2.3",18001))'
  sleep 1
  after=$(curl -fsS http://127.0.0.1:9106/status | python3 -c \
    'import json,sys; print(json.load(sys.stdin)["capturePackets"])')
  tap_after=$(sudo cat /sys/class/net/gxqtap0/statistics/tx_packets)
  test "$after" -gt "$before"
  test "$tap_after" -ge $((tap_before + 2))
  sudo ip netns exec gx-qemu-peer python3 "$peer" --probe \
    --target 10.0.2.15 --port 18001 --attempts 3
  if sudo ip netns exec gx-qemu-peer ping -I gxqiso1 -c 1 -W 1 10.0.2.15 >/dev/null 2>&1; then
    echo "VLAN-isolated interface unexpectedly reached the guest" >&2; return 1
  fi
  sudo grep -q "10.0.2.15" "$run_dir/peer.log"
  curl -fsS http://127.0.0.1:9106/status | python3 -c \
    'import json,sys; value=json.load(sys.stdin); assert value["records"] > 0 and value["capturePackets"] > 0'
  echo "PASS QEMU TAP unicast, guest MAC learning, VLAN isolation, broadcast/multicast forwarding, and OVS SPAN"
}

case ${1:-} in
  print-qemu-command)
    printf '%s\n' qemu-system-x86_64 "${qemu_args[@]}"
    ;;
  up)
    test "$(uname -s)" = Linux; test -x "$graphx"; sudo -v
    for command in qemu-system-x86_64 ovs-vsctl ovs-appctl ip dumpcap setpriv python3 curl; do require "$command"; done
    qemu_identity
    test -r "$images/bzImage" && test -r "$images/rootfs.cpio.gz"
    ! owned_pid qemu.pid qemu-system-x86_64 || { echo "QEMU TAP QEMU is already running" >&2; exit 2; }
    prepare_run_dir
    trap rollback_up ERR
    sudo "$graphx" infra create "$config"
    start_capture
    start_peer
    start_qemu
    verify_network
    trap - ERR
    ;;
  status)
    sudo "$graphx" infra status "$config"
    owned_pid qemu.pid qemu-system-x86_64 && echo "QEMU: running as UID/GID $qemu_uid:$qemu_gid"
    qmp_as_qemu probe --socket "$run_dir/qemu.qmp" \
      --output "$run_dir/accelerator-evidence.json" --requested tcg --selected tcg >/dev/null
    qmp_in_peer guest-readiness --target 10.0.2.15 --port 18001 \
      --socket "$run_dir/qemu.qmp" --output "$run_dir/accelerator-evidence.json" \
      --wait 10 --interval 1 || true
    sudo cat "$run_dir/accelerator-evidence.json"
    ;;
  verify) verify_network ;;
  pause|resume)
    qmp_as_qemu vm-control --socket "$run_dir/qemu.qmp" --action "$1" \
      --output "$run_dir/accelerator-evidence.json"
    if test "$1" = resume; then
      qmp_in_peer guest-readiness --target 10.0.2.15 --port 18001 \
        --socket "$run_dir/qemu.qmp" --output "$run_dir/accelerator-evidence.json" \
        --wait 10 --interval 1
    fi
    ;;
  down)
    stop_runtime
    sudo "$graphx" infra destroy "$config"
    ;;
  *) echo "usage: $0 <up|status|verify|pause|resume|down>" >&2; exit 64 ;;
esac

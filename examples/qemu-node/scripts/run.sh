#!/usr/bin/env bash
set -euo pipefail
umask 077

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
images_dir="$example_dir/output/images"
duration=30
max_capture_bytes=67108864

usage() {
  echo "usage: $0 [--duration 1..3600] [--max-capture-bytes 1048576..1073741824]" >&2
}
while (($#)); do
  case "$1" in
    --duration) duration="${2:-}"; shift 2 ;;
    --max-capture-bytes) max_capture_bytes="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) usage; exit 2 ;;
  esac
done
[[ "$duration" =~ ^[0-9]+$ ]] && ((duration >= 1 && duration <= 3600)) || {
  echo "duration must be an integer from 1 through 3600" >&2; exit 2;
}
[[ "$max_capture_bytes" =~ ^[0-9]+$ ]] &&
  ((max_capture_bytes >= 1048576 && max_capture_bytes <= 1073741824)) || {
  echo "max-capture-bytes must be from 1048576 through 1073741824" >&2; exit 2;
}

command -v qemu-system-x86_64 >/dev/null || {
  echo "Missing qemu-system-x86_64; see $example_dir/README.md" >&2; exit 2;
}
command -v python3 >/dev/null || { echo "Missing required command: python3" >&2; exit 2; }
for image in bzImage rootfs.cpio.gz; do
  [[ -s "$images_dir/$image" ]] || {
    echo "Missing $images_dir/$image; run scripts/build.sh first" >&2; exit 2;
  }
done

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
run_dir="$repo_dir/outputs/qemu-node/$timestamp"
mkdir -p "$run_dir"
capture="$run_dir/qemu-node.pcap"
peer_pid=""
qemu_pid=""
guard_pid=""

cleanup() {
  local status=$?
  trap - EXIT INT TERM
  for pid in "$guard_pid" "$qemu_pid" "$peer_pid"; do
    if [[ -n "$pid" ]]; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [[ -s "$capture" ]]; then
    python3 "$example_dir/tools/capture_history.py" "$capture" \
      "$run_dir/packet-history.sqlite" --max-records 10000 --preview-bytes 64 \
      --pcapng "$run_dir/qemu-node.pcapng" || status=$?
  fi
  echo "QEMU run artifacts: $run_dir"
  exit "$status"
}
trap cleanup EXIT INT TERM

python3 "$example_dir/host/peer.py" --serve >"$run_dir/host-peer.log" 2>&1 &
peer_pid=$!

qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu qemu64 -m 256M -smp 1 \
  -kernel "$images_dir/bzImage" \
  -initrd "$images_dir/rootfs.cpio.gz" \
  -append "console=ttyS0 panic=1" \
  -no-reboot -nographic \
  -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:18001-:18001,hostfwd=udp:127.0.0.1:18001-:18001" \
  -device virtio-net-pci,netdev=net0 \
  -object "filter-dump,id=capture0,netdev=net0,file=$capture" \
  >"$run_dir/guest-console.log" 2>&1 &
qemu_pid=$!

(
  while kill -0 "$qemu_pid" 2>/dev/null; do
    if [[ -f "$capture" ]]; then
      capture_bytes="$(wc -c < "$capture" | tr -d ' ')"
      if ((capture_bytes >= max_capture_bytes)); then
        echo "capture byte limit reached; stopping QEMU" >>"$run_dir/guest-console.log"
        kill "$qemu_pid" 2>/dev/null || true
        exit 0
      fi
    fi
    sleep 1
  done
) &
guard_pid=$!

python3 "$example_dir/host/peer.py" --probe --attempts 30 >"$run_dir/probe.log" 2>&1

elapsed=0
while ((elapsed < duration)) && kill -0 "$qemu_pid" 2>/dev/null; do
  sleep 1
  ((elapsed += 1))
done
kill "$qemu_pid" 2>/dev/null || true
wait "$qemu_pid" 2>/dev/null || true
qemu_pid=""

grep -q "PASS tcp guest echo" "$run_dir/probe.log"
grep -q "PASS udp guest echo" "$run_dir/probe.log"
grep -q "tcp echo: qemu tcp sequence=" "$run_dir/guest-console.log"
grep -q "udp echo: qemu udp sequence=" "$run_dir/guest-console.log"
echo "PASS raw TCP and UDP traffic in both directions"

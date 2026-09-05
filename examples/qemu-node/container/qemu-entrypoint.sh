#!/usr/bin/env bash
set -euo pipefail

artifact_dir=${GRAPHX_QEMU_ARTIFACT_DIR:-/artifacts}
capture_dir=${GRAPHX_QEMU_CAPTURE_DIR:-/captures}
accel=${GRAPHX_QEMU_ACCEL:-tcg}
requested_accel=${GRAPHX_QEMU_REQUESTED_ACCEL:-auto}
qmp_socket=/run/graphx-qemu/qemu.qmp
evidence_file="$capture_dir/accelerator-evidence.json"
qmp_control=/opt/graphx-qemu/tools/qmp_control.py
case "$accel" in
  kvm)
    test -c /dev/kvm && test -r /dev/kvm && test -w /dev/kvm || {
      echo "KVM requested but /dev/kvm is not accessible inside qemu-node" >&2
      exit 2
    }
    machine_accel=kvm
    cpu=host
    ;;
  tcg)
    machine_accel=tcg
    cpu=qemu64
    ;;
  *) echo "GRAPHX_QEMU_ACCEL must be kvm or tcg" >&2; exit 2 ;;
esac

for image in bzImage rootfs.cpio.gz; do
  test -s "$artifact_dir/$image" || {
    echo "Missing guest artifact: $artifact_dir/$image" >&2
    exit 2
  }
done
python3 /opt/graphx-qemu/tools/artifact_manifest.py verify --images "$artifact_dir"
mkdir -p "$capture_dir"
rm -f "$capture_dir/qemu-node.pcap" "$capture_dir/qemu-node.pcapng" "$qmp_socket"
python3 "$qmp_control" state --output "$evidence_file" --state booting

python3 /opt/graphx-qemu/host/relay.py --bind 0.0.0.0 --port 19001 \
  --target host-receiver &
relay_pid=$!
readiness_pid=
cleanup() {
  if test -n "$readiness_pid"; then
    kill "$readiness_pid" 2>/dev/null || true
    wait "$readiness_pid" 2>/dev/null || true
  fi
  kill "$relay_pid" 2>/dev/null || true
  wait "$relay_pid" 2>/dev/null || true
}
shutdown_qemu() {
  python3 "$qmp_control" shutdown --socket "$qmp_socket" --wait 3 >/dev/null 2>&1 || true
  for _ in 1 2 3 4 5; do kill -0 "$qemu_pid" 2>/dev/null || return; sleep 1; done
  kill "$qemu_pid" 2>/dev/null || true
}

echo "Starting containerized QEMU with accelerator $accel"
qemu-system-x86_64 \
  -machine "q35,accel=$machine_accel" -cpu "$cpu" -m 256M -smp 1 \
  -kernel "$artifact_dir/bzImage" -initrd "$artifact_dir/rootfs.cpio.gz" \
  -append "console=ttyS0 panic=1" -no-reboot -nographic \
  -qmp "unix:$qmp_socket,server=on,wait=off" \
  -netdev "user,id=net0,hostfwd=tcp:0.0.0.0:18001-:18001,hostfwd=udp:0.0.0.0:18001-:18001" \
  -device virtio-net-pci,netdev=net0 \
  -object "filter-dump,id=capture0,netdev=net0,file=$capture_dir/qemu-node.pcap" &
qemu_pid=$!
stopping=false
handle_signal() {
  stopping=true
  shutdown_qemu
}
trap cleanup EXIT
trap handle_signal INT TERM
if ! python3 "$qmp_control" probe --socket "$qmp_socket" --output "$evidence_file" \
    --requested "$requested_accel" --selected "$accel"; then
  shutdown_qemu
  wait "$qemu_pid" 2>/dev/null || true
  exit 1
fi
if ! python3 "$qmp_control" guest-readiness --target 127.0.0.1 --port 18001 \
    --socket "$qmp_socket" --output "$evidence_file" --wait 60 --interval 2; then
  shutdown_qemu
  wait "$qemu_pid" 2>/dev/null || true
  exit 1
fi
python3 "$qmp_control" guest-readiness --target 127.0.0.1 --port 18001 \
  --socket "$qmp_socket" --output "$evidence_file" --wait 0 --interval 2 \
  --failure-threshold 3 --monitor &
readiness_pid=$!
max_capture_bytes=${GRAPHX_CAPTURE_SOURCE_MAX_BYTES:-67108864}
(
  while kill -0 "$qemu_pid" 2>/dev/null; do
    if test -f "$capture_dir/qemu-node.pcap"; then
      bytes=$(wc -c <"$capture_dir/qemu-node.pcap")
      if test "$bytes" -ge "$max_capture_bytes"; then
        echo "QEMU source capture reached its $max_capture_bytes byte bound" >&2
        kill "$qemu_pid" 2>/dev/null || true
        break
      fi
    fi
    sleep 1
  done
) &
guard_pid=$!
set +e
wait "$qemu_pid"
status=$?
set -e
kill "$guard_pid" 2>/dev/null || true
wait "$guard_pid" 2>/dev/null || true
kill "$readiness_pid" 2>/dev/null || true
wait "$readiness_pid" 2>/dev/null || true
readiness_pid=
if test "$status" -eq 0 || test "$stopping" = true; then
  python3 "$qmp_control" state --output "$evidence_file" --state stopped || true
else
  python3 "$qmp_control" state --output "$evidence_file" --state degraded \
    --reason "QEMU exited with status $status" || true
fi
exit "$status"

#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
graphx_cli=${1:-}
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-qemu-test.XXXXXX")"
trap 'rm -f "$test_dir/qemu-network-node" "$test_dir/exporter/exporter.pid"; rmdir "$test_dir/exporter" "$test_dir" 2>/dev/null || true' EXIT INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
  "$example_dir/guest/src/qemu_network_node.c" -o "$test_dir/qemu-network-node"

python3 -m py_compile \
  "$example_dir/host/peer.py" \
  "$example_dir/tools/artifact_manifest.py" \
  "$example_dir/tools/packet_observer.py" \
  "$example_dir/tools/qmp_control.py"

bash -n \
  "$example_dir/scripts/build.sh" \
  "$example_dir/scripts/demo.sh" \
  "$example_dir/scripts/inspect-capture.sh" \
  "$example_dir/tools/capture_exporter.sh" \
  "$example_dir/tap/scripts/ovs-lab.sh"

mkdir "$test_dir/exporter"
"$example_dir/tools/capture_exporter.sh" "$test_dir/exporter/exporter.pid" \
  /usr/bin/false /dev/null qemu-span "$test_dir/exporter" "$(id -g)" &
exporter_process=$!
for _ in {1..20}; do
  test -s "$test_dir/exporter/exporter.pid" && break
  sleep 0.05
done
test -s "$test_dir/exporter/exporter.pid"
kill "$(cat "$test_dir/exporter/exporter.pid")"
wait "$exporter_process"
test ! -e "$test_dir/exporter/exporter.pid"
rmdir "$test_dir/exporter"

if [[ -n "$graphx_cli" ]]; then
  GRAPHX_OVERRIDES= "$graphx_cli" validate "$example_dir/tap/graphx.yaml"
fi

echo "GraphX QEMU TAP checks passed"

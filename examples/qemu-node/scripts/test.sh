#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
graphx_cli=${1:-}
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-qemu-test.XXXXXX")"
trap 'rm -f "$test_dir/qemu-network-node"; rmdir "$test_dir" 2>/dev/null || true' EXIT INT TERM

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
  "$example_dir/tap/scripts/ovs-lab.sh"

if [[ -n "$graphx_cli" ]]; then
  GRAPHX_OVERRIDES= "$graphx_cli" validate "$example_dir/tap/graphx.yaml"
fi

echo "GraphX QEMU TAP checks passed"

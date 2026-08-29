#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
side="${1:-mac}"
[[ "$side" == mac || "$side" == ipv ]] || { echo "usage: $0 [mac|ipv]" >&2; exit 2; }
if docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" ps -q | grep -q .; then
  exec docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router \
    tcpdump -ni "mirror-$side"
fi
exec sudo tcpdump -ni "cap-$side"

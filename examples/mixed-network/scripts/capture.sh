#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
side="${1:-mac}"
output="${2:-}"
[[ "$side" == mac || "$side" == ipv ]] || { echo "usage: $0 [mac|ipv] [output.pcapng]" >&2; exit 2; }
if docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" ps -q | grep -q .; then
  if [[ -n "$output" ]]; then
    echo "Saving standard Ethernet packets from mirror-$side to $output"
    exec docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec -T ovs-router \
      dumpcap -q -i "mirror-$side" -w - >"$output"
  fi
  exec docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router \
    tcpdump -ni "mirror-$side"
fi
if [[ -n "$output" ]]; then
  command -v dumpcap >/dev/null || { echo "dumpcap is required to save PCAPNG; install Wireshark CLI tools" >&2; exit 2; }
  echo "Saving standard Ethernet packets from cap-$side to $output"
  exec sudo dumpcap -q -i "cap-$side" -w "$output"
fi
exec sudo tcpdump -ni "cap-$side"

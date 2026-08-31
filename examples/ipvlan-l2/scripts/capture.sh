#!/usr/bin/env bash
set -euo pipefail
side="${1:-generator}"
output="${2:-}"
case "$side" in
  generator) interface=l2g-cap ;;
  transform) interface=l2t-cap ;;
  sink) interface=l2s-cap ;;
  *) echo "usage: $0 generator|transform|sink [output.pcapng]" >&2; exit 2 ;;
esac
if [[ -n "$output" ]]; then
  command -v dumpcap >/dev/null || { echo "dumpcap is required to save PCAPNG; install Wireshark CLI tools" >&2; exit 2; }
  echo "Saving standard Ethernet packets from $interface to $output"
  exec sudo dumpcap -q -i "$interface" -w "$output"
fi
exec sudo tcpdump -n -i "$interface"

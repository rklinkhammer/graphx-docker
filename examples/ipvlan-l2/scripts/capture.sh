#!/usr/bin/env bash
set -euo pipefail
side="${1:-generator}"
case "$side" in
  generator) interface=l2g-cap ;;
  transform) interface=l2t-cap ;;
  sink) interface=l2s-cap ;;
  *) echo "usage: $0 generator|transform|sink" >&2; exit 2 ;;
esac
exec sudo tcpdump -n -i "$interface"

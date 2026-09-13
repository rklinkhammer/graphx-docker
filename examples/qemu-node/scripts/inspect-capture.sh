#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 EXPORTED_PCAPNG [DISPLAY_FILTER]" >&2
  exit 64
fi
exec "${TSHARK:-tshark}" -r "$1" -Y "${2:-tcp || udp}"

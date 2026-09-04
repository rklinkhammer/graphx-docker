#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -s)" == Linux ]] || {
  echo "Native UDP broadcast cleanup requires Linux" >&2
  exit 2
}
command -v ip >/dev/null || { echo "Missing required command: ip" >&2; exit 2; }
command -v sudo >/dev/null || { echo "Missing required command: sudo" >&2; exit 2; }

for namespace in gx-udp-publisher gx-udp-listener; do
  while read -r process; do
    [[ -n "$process" ]] && sudo kill "$process" 2>/dev/null || true
  done < <(sudo ip netns pids "$namespace" 2>/dev/null || true)
  sudo ip netns delete "$namespace" 2>/dev/null || true
done
sudo ip link delete gxudp-br 2>/dev/null || true


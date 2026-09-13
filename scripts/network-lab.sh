#!/usr/bin/env bash

# Authored v3 execution is not yet implemented. Sourced library functions remain available.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "E_PHASE_UNAVAILABLE: GraphX v3 execution adapters are not implemented; no action was performed" >&2
  exit 2
fi
set -euo pipefail

ROOT=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
# shellcheck source=lib/demo-runtime.sh
source "${ROOT}/scripts/lib/demo-runtime.sh"

usage() {
  cat <<'EOF'
usage: scripts/network-lab.sh <macvlan|ipvlan-l2|ipvlan-l3|mixed-network> <plan|up|status|down>

Runs the selected system-OVS laboratory directly on Linux or inside the
identity-checked GraphX Lima VM on Apple Silicon macOS.
EOF
}

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 64
fi

graphx_demo_network_lab "$ROOT" "$1" "$2" || {
  status=$?
  if [[ $status == 64 ]]; then usage >&2; fi
  exit "$status"
}

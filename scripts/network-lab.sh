#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
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

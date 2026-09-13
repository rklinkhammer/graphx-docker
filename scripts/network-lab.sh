#!/usr/bin/env bash

set -euo pipefail

ROOT=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
# shellcheck source=lib/demo-runtime.sh
source "${ROOT}/scripts/lib/demo-runtime.sh"

usage() {
  cat <<'EOF'
usage: scripts/network-lab.sh <macvlan|ipvlan-l2|ipvlan-l3|mixed-network> <plan|up|status|down>

Uses GX_OUTPUT, GX_STATE and GRAPHX_IMAGE_RELEASE for an existing compilation.
Set GRAPHX_ALLOW_PRIVILEGED=1 for up/status/down. On macOS these are guest paths
and execution uses the existing identity-checked GraphX Lima VM.
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

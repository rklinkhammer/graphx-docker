#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
# shellcheck source=scripts/lib/demo-runtime.sh
source "$ROOT/scripts/lib/demo-runtime.sh"
case "${1:-status}" in
  apply-route|clear-route)
    echo "E_PHASE_UNAVAILABLE: scenario route actions are not implemented" >&2
    exit 2
    ;;
esac
graphx_demo_run "${1:-status}"

#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
# shellcheck source=scripts/lib/demo-runtime.sh
source "$ROOT/scripts/lib/demo-runtime.sh"
case "${1:-status}" in
  apply-route) graphx_demo_scenario run apply-deferred; exit ;;
  clear-route) graphx_demo_scenario run clear-deferred; exit ;;
  verify) graphx_demo_scenario run verify-flows; exit ;;
esac
graphx_demo_run "${1:-status}"

#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# shellcheck source=scripts/lib/demo-runtime.sh
source "$ROOT/scripts/lib/demo-runtime.sh"
graphx_demo_run "${1:-status}"

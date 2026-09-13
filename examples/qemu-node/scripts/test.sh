#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
python3 "$ROOT/tests/test_guest_agent.py" "$ROOT"
if [[ -n ${1:-} ]]; then
  "$1" validate "$ROOT/examples/qemu-node/tap/graphx.yml"
fi

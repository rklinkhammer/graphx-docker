#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
"$ROOT/scripts/require-node24.sh"
cd "$ROOT/apps/telemetry"
exec node --test operations.test.mjs platform.test.mjs

#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
source "$ROOT/scripts/test-feature-common.sh"
BUILD_DIR=${GRAPHX_BUILD_DIR:-"$ROOT/build/dev"}
require_node24
npm ci --prefix "$ROOT/apps/telemetry" --no-audit --no-fund
NORMALIZED_CONFIG_CLI="$BUILD_DIR/graphx" without_graphx_environment npm test --prefix "$ROOT/apps/telemetry"
npm ci --prefix "$ROOT/web" --no-audit --no-fund
without_graphx_environment npm test --prefix "$ROOT/web"
npm run build --prefix "$ROOT/web"
python3 "$ROOT/tests/test_execution.py" "$BUILD_DIR" "$ROOT"
echo "Normalized consumers, HTTP integration, web console and native execution checks passed"

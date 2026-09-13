#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
: "${GRAPHX_IMAGE_RELEASE:?Set GRAPHX_IMAGE_RELEASE to a verified image_release directory}"
output="$ROOT/outputs/platform-engine-$(date -u +%Y%m%dT%H%M%SZ)-$$"
exec python3 "$ROOT/tests/test_execution_matrix.py" "$GRAPHX_IMAGE_RELEASE" --output "$output"

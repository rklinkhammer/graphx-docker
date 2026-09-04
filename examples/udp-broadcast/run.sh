#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$example_dir/../../scripts/configure-build-trust.sh"
cleanup() { docker compose -f "$example_dir/compose.yaml" down --remove-orphans >/dev/null 2>&1 || true; }
trap cleanup EXIT INT TERM
cleanup
image="${GRAPHX_BROADCAST_IMAGE:-graphx-demo:latest}"
docker image inspect "$image" >/dev/null 2>&1 || {
  echo "Missing local image '$image'. Build or load it before running this offline example." >&2
  echo "For a connected one-time build: docker build -t '$image' '$example_dir/../..'" >&2
  exit 2
}
docker compose -f "$example_dir/compose.yaml" config --quiet
docker compose -f "$example_dir/compose.yaml" up --no-build --pull never --abort-on-container-exit \
  --exit-code-from listener

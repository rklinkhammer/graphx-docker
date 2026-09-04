#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
status=0
docker compose -p gx-ipvl3-generator -f "$example_dir/compose/generator.compose.yml" \
  down --remove-orphans || status=$?
docker compose -p gx-ipvl3-transform -f "$example_dir/compose/transform.compose.yml" \
  down --remove-orphans || status=$?
docker compose -p gx-ipvl3-sink -f "$example_dir/compose/sink.compose.yml" \
  down --remove-orphans || status=$?
if [[ -x "$graphx" ]]; then
  sudo "$graphx" infra destroy "$example_dir/graphx.yaml" || status=$?
else
  echo "GraphX CLI is unavailable at $graphx; applying named-resource fallback cleanup." >&2
  status=2
fi
# Remove names from the original, invalid one-network-per-parent layout so this
# helper also repairs a partially failed older run.
docker network rm gx-ipvl3-domains gx-ipvl3-generator gx-ipvl3-transform gx-ipvl3-sink \
  >/dev/null 2>&1 || true
sudo ip link delete gx-l3-parent 2>/dev/null || true
exit "$status"

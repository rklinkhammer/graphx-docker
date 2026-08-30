#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
docker compose -p gx-ipvl3-generator -f "$example_dir/compose/generator.compose.yml" down --remove-orphans || true
docker compose -p gx-ipvl3-transform -f "$example_dir/compose/transform.compose.yml" down --remove-orphans || true
docker compose -p gx-ipvl3-sink -f "$example_dir/compose/sink.compose.yml" down --remove-orphans || true
sudo "$repo_dir/build/dev/graphx" infra destroy "$example_dir/graphx.yaml"
# Remove names from the original, invalid one-network-per-parent layout so this
# helper also repairs a partially failed older run.
docker network rm gx-ipvl3-generator gx-ipvl3-transform gx-ipvl3-sink \
  >/dev/null 2>&1 || true
sudo ip link delete gx-l3-parent 2>/dev/null || true

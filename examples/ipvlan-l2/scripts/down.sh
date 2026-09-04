#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
docker compose -p gx-ipvl2-generator -f "$example_dir/compose/generator.compose.yml" down --remove-orphans || true
docker compose -p gx-ipvl2-transform -f "$example_dir/compose/transform.compose.yml" down --remove-orphans || true
docker compose -p gx-ipvl2-sink -f "$example_dir/compose/sink.compose.yml" down --remove-orphans || true
sudo "$graphx" infra destroy "$example_dir/graphx.yaml"

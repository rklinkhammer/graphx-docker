#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
[[ "$(uname -s)" == Linux ]] || { echo "ipvlan L2 demo requires a Linux Docker host" >&2; exit 2; }
sudo "$graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-ipvl2-generator -f "$example_dir/compose/generator.compose.yml" up -d --build
docker compose -p gx-ipvl2-transform -f "$example_dir/compose/transform.compose.yml" up -d
docker compose -p gx-ipvl2-sink -f "$example_dir/compose/sink.compose.yml" up -d

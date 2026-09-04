#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
source "$repo_dir/scripts/configure-build-trust.sh"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
[[ "$(uname -s)" == Linux ]] || { echo "macvlan demo requires a Linux Docker host" >&2; exit 2; }
sudo ip link show gx-mac-parent >/dev/null 2>&1 || sudo ip link add gx-mac-parent type dummy
sudo ip link set gx-mac-parent up
sudo "$graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-mac-generator -f "$example_dir/compose/generator.compose.yml" up -d --build
docker compose -p gx-mac-transform -f "$example_dir/compose/transform.compose.yml" up -d
docker compose -p gx-mac-sink -f "$example_dir/compose/sink.compose.yml" up -d

#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
[[ "$(uname -s)" == Linux ]] || { echo "ipvlan L3 demo requires a Linux Docker host" >&2; exit 2; }
sudo ip link show gx-l3-parent >/dev/null 2>&1 || sudo ip link add gx-l3-parent type dummy
sudo ip address replace 192.0.2.1/30 dev gx-l3-parent
sudo ip link set gx-l3-parent up
sudo "$repo_dir/build/dev/graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-ipvl3-generator -f "$example_dir/compose/generator.compose.yml" up -d --build
docker compose -p gx-ipvl3-transform -f "$example_dir/compose/transform.compose.yml" up -d
docker compose -p gx-ipvl3-sink -f "$example_dir/compose/sink.compose.yml" up -d

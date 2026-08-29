#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
[[ "$(uname -s)" == Linux ]] || { echo "native profile requires Linux" >&2; exit 2; }
sudo "$repo_dir/build/dev/graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-mac-side -f "$example_dir/compose/macvlan.compose.yml" up -d --build
docker compose -p gx-ipv-side -f "$example_dir/compose/ipvlan.compose.yml" up -d --build

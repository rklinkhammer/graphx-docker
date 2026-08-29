#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
docker compose -p gx-mac-side -f "$example_dir/compose/macvlan.compose.yml" down --remove-orphans
docker compose -p gx-ipv-side -f "$example_dir/compose/ipvlan.compose.yml" down --remove-orphans
sudo "$repo_dir/build/dev/graphx" infra destroy "$example_dir/graphx.yaml"

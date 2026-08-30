#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
status=0
docker compose -p gx-mac-side -f "$example_dir/compose/macvlan.compose.yml" \
  down --remove-orphans || status=$?
docker compose -p gx-ipv-side -f "$example_dir/compose/ipvlan.compose.yml" \
  down --remove-orphans || status=$?
sudo "$repo_dir/build/dev/graphx" infra destroy "$example_dir/graphx.yaml" || status=$?
if ((status == 0)); then
  echo "Mixed-network containers and infrastructure removed."
fi
exit "$status"

#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
sudo "$graphx" infra status "$example_dir/graphx.yaml"
for project in gx-ipvl3-generator gx-ipvl3-transform gx-ipvl3-sink; do
  docker compose -p "$project" -f "$example_dir/compose/${project#gx-ipvl3-}.compose.yml" ps
done
echo
echo "Latest values received across all three L3 subnets:"
docker logs --tail 20 gx-ipvl3-sink-sink-1 2>&1 | grep 'sink seq=' | tail -n 5 ||
  echo "No sink samples observed. Run the sink logs and inspect container startup errors."

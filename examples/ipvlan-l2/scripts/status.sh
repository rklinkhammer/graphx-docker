#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
sudo "$repo_dir/build/dev/graphx" infra status "$example_dir/graphx.yaml"
for project in gx-ipvl2-generator gx-ipvl2-transform gx-ipvl2-sink; do
  docker compose -p "$project" -f "$example_dir/compose/${project#gx-ipvl2-}.compose.yml" ps
done

#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
docker compose -p gx-mac-generator -f "$example_dir/compose/generator.compose.yml" down --remove-orphans || true
docker compose -p gx-mac-transform -f "$example_dir/compose/transform.compose.yml" down --remove-orphans || true
docker compose -p gx-mac-sink -f "$example_dir/compose/sink.compose.yml" down --remove-orphans || true
sudo "$repo_dir/build/dev/graphx" infra destroy "$example_dir/graphx.yaml"
sudo ip link delete gx-mac-parent 2>/dev/null || true

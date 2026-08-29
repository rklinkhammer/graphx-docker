#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" ps -q | grep -q .; then
  docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router ovs-vsctl show
  docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router ip -br address
else
  repo_dir="$(cd "$example_dir/../.." && pwd)"
  sudo "$repo_dir/build/dev/graphx" infra status "$example_dir/graphx.yaml"
fi

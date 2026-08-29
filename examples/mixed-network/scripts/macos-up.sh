#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker network inspect gx-mac-sim >/dev/null 2>&1 || \
  docker network create --driver bridge --subnet 10.10.0.0/24 --gateway 10.10.0.254 gx-mac-sim
docker network inspect gx-ipv-sim >/dev/null 2>&1 || \
  docker network create --driver bridge --subnet 10.20.0.0/24 --gateway 10.20.0.254 gx-ipv-sim
docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" up -d --build
docker compose -p gx-mac-side -f "$example_dir/compose/macos-mac.compose.yml" up -d --build
docker compose -p gx-ipv-side -f "$example_dir/compose/macos-ipv.compose.yml" up -d --build
echo "OVS startup: docker compose -p gx-ovs -f $example_dir/compose/macos-ovs.compose.yml logs -f"

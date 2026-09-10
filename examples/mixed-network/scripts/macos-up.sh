#!/usr/bin/env bash
set -euo pipefail

echo "NOTICE: this is the legacy Docker Desktop userspace-OVS simulation." >&2
echo "Use scripts/ovs-up.sh inside the GraphX Lima VM for the M5 topology." >&2
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
source "$repo_dir/scripts/configure-build-trust.sh"
docker network inspect gx-mac-sim >/dev/null 2>&1 || \
  docker network create --driver bridge --subnet 10.10.0.0/24 --gateway 10.10.0.254 gx-mac-sim
docker network inspect gx-ipv-sim >/dev/null 2>&1 || \
  docker network create --driver bridge --subnet 10.20.0.0/24 --gateway 10.20.0.254 gx-ipv-sim
docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" up -d --build
docker compose -p gx-mac-side -f "$example_dir/compose/macos-mac.compose.yml" up -d --build
docker compose -p gx-ipv-side -f "$example_dir/compose/macos-ipv.compose.yml" up -d --build
echo "OVS startup: docker compose -p gx-ovs -f $example_dir/compose/macos-ovs.compose.yml logs -f"

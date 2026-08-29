#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
docker compose -p gx-mac-side -f "$example_dir/compose/macos-mac.compose.yml" down --remove-orphans
docker compose -p gx-ipv-side -f "$example_dir/compose/macos-ipv.compose.yml" down --remove-orphans
docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" down --remove-orphans
docker network rm gx-mac-sim gx-ipv-sim >/dev/null 2>&1 || true

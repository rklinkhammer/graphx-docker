#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
action="${1:-apply}"
if docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" ps -q | grep -q .; then
  if [[ "$action" == clear ]]; then
    docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router \
      tc qdisc delete dev br-gx-ipv root || true
    exit 0
  fi
  exec docker compose -p gx-ovs -f "$example_dir/compose/macos-ovs.compose.yml" exec ovs-router \
    tc qdisc replace dev br-gx-ipv root netem delay "${DELAY:-20ms}" "${JITTER:-3ms}" loss "${LOSS:-1%}"
fi
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
if [[ "$action" == clear ]]; then
  exec sudo "$graphx" infra fault clear "$example_dir/graphx.yaml" \
    --router domain-router --interface ipv
fi
exec sudo "$graphx" infra fault apply "$example_dir/graphx.yaml" \
  --router domain-router --interface ipv --delay "${DELAY:-20ms}" --jitter "${JITTER:-3ms}" --loss "${LOSS:-1%}"

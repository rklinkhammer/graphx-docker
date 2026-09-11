#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: network-lab-ovs.sh <up|down|status> EXAMPLE_DIR COMPOSE_FILE" >&2
  exit 64
fi

action=$1
example_dir=$(cd "$2" && pwd)
compose_file=$3
repo_root=$(cd "$example_dir/../.." && pwd)
graphx=${GRAPHX_BIN:-$repo_root/build/dev/graphx}
config=$example_dir/graphx.yaml

if [[ $(uname -s) != Linux ]]; then
  echo "Run this topology inside the GraphX Lima VM (or on native Linux)." >&2
  exit 1
fi
if [[ ! -x $graphx ]]; then
  echo "GraphX CLI not found at $graphx; build the dev preset first." >&2
  exit 1
fi

case "$action" in
  up)
    sudo docker compose -f "$example_dir/$compose_file" up -d --build
    if ! sudo "$graphx" infra create "$config"; then
      sudo docker compose -f "$example_dir/$compose_file" down
      exit 1
    fi
    sudo "$graphx" infra status "$config"
    ;;
  status)
    sudo "$graphx" infra status "$config"
    sudo docker compose -f "$example_dir/$compose_file" ps
    ;;
  down)
    sudo "$graphx" infra destroy "$config"
    sudo docker compose -f "$example_dir/$compose_file" down
    ;;
  *)
    echo "unsupported action: $action" >&2
    exit 64
    ;;
esac

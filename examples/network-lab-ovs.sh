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
# shellcheck source=../scripts/lib/demo-runtime.sh
source "$repo_root/scripts/lib/demo-runtime.sh"
graphx_demo_ovs_lab "$action" "$example_dir" "$compose_file" "$repo_root"

#!/usr/bin/env bash

# Authored v3 execution is not yet implemented. Sourced library functions remain available.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "E_PHASE_UNAVAILABLE: GraphX v3 execution adapters are not implemented; no action was performed" >&2
  exit 2
fi
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: network-lab-ovs.sh <macvlan|ipvlan-l2|ipvlan-l3|mixed-network> <up|down|status>" >&2
  exit 64
fi

lab=$1
action=$2
script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
example_dir="$script_dir/$lab"
repo_root=$(cd "$script_dir/.." && pwd)
case "$lab" in
  mixed-network) project=mixed-network ;;
  macvlan|ipvlan-l2|ipvlan-l3) project="$lab-pipeline" ;;
  *) echo "unsupported network laboratory: $lab" >&2; exit 64 ;;
esac
# shellcheck source=../scripts/lib/demo-runtime.sh
source "$repo_root/scripts/lib/demo-runtime.sh"
graphx_demo_ovs_lab "$action" "$example_dir" "$project" "$repo_root"

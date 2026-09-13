#!/usr/bin/env bash

# Authored v3 execution is not yet implemented. Sourced library functions remain available.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "E_PHASE_UNAVAILABLE: GraphX v3 execution adapters are not implemented; no action was performed" >&2
  exit 2
fi
set -euo pipefail
script_dir=$(cd "$(dirname "$0")" && pwd)
exec "$script_dir/../../network-lab-ovs.sh" ipvlan-l3 status

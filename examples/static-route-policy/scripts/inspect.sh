#!/usr/bin/env bash

# Authored v3 execution is not yet implemented. Sourced library functions remain available.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "E_PHASE_UNAVAILABLE: GraphX v3 execution adapters are not implemented; no action was performed" >&2
  exit 2
fi
set -euo pipefail
example_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
graphx=${GRAPHX_BIN:-$repo_dir/build/dev/graphx}
test -x "$graphx" || { echo "GraphX CLI not found; set GRAPHX_BIN or build the dev preset" >&2; exit 2; }
"$graphx" validate "$example_dir/graphx.yml"
"$graphx" inspect "$example_dir/graphx.yml"
"$graphx" infra create "$example_dir/graphx.yml" --dry-run
"$graphx" infra route apply "$example_dir/graphx.yml" --router route-router \
  --destination 10.64.30.10/32 --dry-run
"$graphx" infra route clear "$example_dir/graphx.yml" --router route-router \
  --destination 10.64.30.10/32 --dry-run
"$graphx" infra status "$example_dir/graphx.yml" --dry-run
"$graphx" infra destroy "$example_dir/graphx.yml" --dry-run

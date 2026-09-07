#!/usr/bin/env bash
set -euo pipefail
example_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
graphx=${GRAPHX_BIN:-$repo_dir/build/dev/graphx}
test -x "$graphx" || { echo "GraphX CLI not found; set GRAPHX_BIN or build the dev preset" >&2; exit 2; }
"$graphx" validate "$example_dir/graphx.yaml"
"$graphx" inspect "$example_dir/graphx.yaml"
"$graphx" infra create "$example_dir/graphx.yaml" --dry-run
"$graphx" infra route apply "$example_dir/graphx.yaml" --router route-router \
  --destination 10.64.30.10/32 --dry-run
"$graphx" infra route clear "$example_dir/graphx.yaml" --router route-router \
  --destination 10.64.30.10/32 --dry-run
"$graphx" infra status "$example_dir/graphx.yaml" --dry-run
"$graphx" infra destroy "$example_dir/graphx.yaml" --dry-run

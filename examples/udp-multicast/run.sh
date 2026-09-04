#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
build_dir="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}"
log_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-udp-multicast.XXXXXX")"
pids=()
cleanup() {
  set +u
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
  find "$log_dir" -type f -delete
  rmdir "$log_dir" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
[[ -x "$build_dir/graphx-udp-publisher" && -x "$build_dir/graphx-udp-subscriber" ]] || {
  echo "Build graphx-udp-publisher and graphx-udp-subscriber first" >&2
  exit 2
}
export GRAPHX_CONFIG="$example_dir/graphx.yaml" GRAPHX_MAX_MESSAGES="${GRAPHX_MAX_MESSAGES:-5}"
export GRAPHX_START_DELAY_MS="${GRAPHX_START_DELAY_MS:-200}"
GRAPHX_NODE=subscriber "$build_dir/graphx-udp-subscriber" >"$log_dir/subscriber.log" 2>&1 & pids+=("$!")
GRAPHX_NODE=diagnostic "$build_dir/graphx-udp-subscriber" >"$log_dir/diagnostic.log" 2>&1 & pids+=("$!")
"$build_dir/graphx-udp-publisher"
status=0
for pid in "${pids[@]}"; do wait "$pid" || status=$?; done
pids=()
cat "$log_dir/subscriber.log" "$log_dir/diagnostic.log"
exit "$status"

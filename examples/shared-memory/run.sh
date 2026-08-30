#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
build_dir="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}"
message_count="${GRAPHX_MAX_MESSAGES:-20}"
log_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-shared-memory.XXXXXX")"
pids=()
cleanup() {
  set +u
  for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
  for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
}
trap cleanup EXIT INT TERM
export GRAPHX_CONFIG="$example_dir/graphx.yaml"
export GRAPHX_INTERVAL_MS="${GRAPHX_INTERVAL_MS:-50}"
export GRAPHX_MAX_MESSAGES="$message_count"
"$build_dir/graphx-sink" >"$log_dir/sink.log" 2>&1 & pids+=("$!")
"$build_dir/graphx-transform" >"$log_dir/transform.log" 2>&1 & pids+=("$!")
"$build_dir/graphx-generator" \
  >"$log_dir/generator.log" 2>&1 & pids+=("$!")
status=0
for pid in "${pids[@]}"; do wait "$pid" || status=$?; done
pids=()
cat "$log_dir/sink.log"
echo "logs: $log_dir"
exit "$status"

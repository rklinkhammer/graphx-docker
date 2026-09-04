#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
build_dir="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}"
log_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-udp-unicast.XXXXXX")"
subscriber_pid=""
cleanup() {
  if [[ -n "$subscriber_pid" ]]; then
    kill "$subscriber_pid" 2>/dev/null || true
    wait "$subscriber_pid" 2>/dev/null || true
  fi
  rm -f "$log_dir/subscriber.log"
  rmdir "$log_dir" 2>/dev/null || true
}
trap cleanup EXIT INT TERM
[[ -x "$build_dir/graphx-udp-publisher" && -x "$build_dir/graphx-udp-subscriber" ]] || {
  echo "Build graphx-udp-publisher and graphx-udp-subscriber first" >&2
  exit 2
}
export GRAPHX_CONFIG="$example_dir/graphx.yaml" GRAPHX_MAX_MESSAGES="${GRAPHX_MAX_MESSAGES:-5}"
export GRAPHX_START_DELAY_MS="${GRAPHX_START_DELAY_MS:-200}"
"$build_dir/graphx-udp-subscriber" >"$log_dir/subscriber.log" 2>&1 &
subscriber_pid=$!
"$build_dir/graphx-udp-publisher"
wait "$subscriber_pid"
subscriber_pid=""
cat "$log_dir/subscriber.log"

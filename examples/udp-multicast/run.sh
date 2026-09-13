#!/usr/bin/env bash

# Authored v3 execution is not yet implemented. Sourced library functions remain available.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  echo "E_PHASE_UNAVAILABLE: GraphX v3 execution adapters are not implemented; no action was performed" >&2
  exit 2
fi
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
build_dir="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}"
log_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-udp-multicast.XXXXXX")"
pids=()
# Called indirectly by the EXIT/INT/TERM traps below.
# shellcheck disable=SC2329
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
export GRAPHX_CONFIG="$example_dir/graphx.yml" GRAPHX_MAX_MESSAGES="${GRAPHX_MAX_MESSAGES:-5}"
# Give instrumented or freshly loaded subscribers time to join before the
# publisher sends its finite, intentionally unretried datagram sequence.
export GRAPHX_START_DELAY_MS="${GRAPHX_START_DELAY_MS:-1000}"
GRAPHX_NODE=subscriber "$build_dir/graphx-udp-subscriber" >"$log_dir/subscriber.log" 2>&1 & pids+=("$!")
GRAPHX_NODE=diagnostic "$build_dir/graphx-udp-subscriber" >"$log_dir/diagnostic.log" 2>&1 & pids+=("$!")
"$build_dir/graphx-udp-publisher"
status=0
for pid in "${pids[@]}"; do wait "$pid" || status=$?; done
pids=()
cat "$log_dir/subscriber.log" "$log_dir/diagnostic.log"
exit "$status"

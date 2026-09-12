#!/usr/bin/env bash

step() { printf '\n==> %s\n' "$*"; }
require() { command -v "$1" >/dev/null || { echo "missing prerequisite: $1" >&2; exit 2; }; }
require_node24() { bash "$ROOT/scripts/require-node24.sh"; }

without_graphx_environment() {
  local name
  local -a command=(env)
  while IFS='=' read -r name _; do
    case "$name" in
      GRAPHX_*) command+=(-u "$name") ;;
    esac
  done < <(env)
  "${command[@]}" "$@"
}

wait_for_exit() {
  local pid=$1 name=$2
  for _ in {1..100}; do
    if ! kill -0 "$pid" 2>/dev/null; then wait "$pid"; return; fi
    sleep 0.05
  done
  echo "$name did not stop cleanly" >&2
  return 1
}

configuration_version() {
  awk '$1 == "version:" { print $2; exit }' "$1"
}

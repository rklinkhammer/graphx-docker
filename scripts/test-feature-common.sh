#!/usr/bin/env bash

step() { printf '\n==> %s\n' "$*"; }
require() { command -v "$1" >/dev/null || { echo "missing prerequisite: $1" >&2; exit 2; }; }
require_node24() {
  if bash "$ROOT/scripts/require-node24.sh" >/dev/null 2>&1; then
    return 0
  fi
  # Homebrew keeps node@24 separate from the default Node installation. Select
  # its complete toolchain for this verification process and its children only.
  local prefix version
  if command -v brew >/dev/null 2>&1; then
    prefix=$(brew --prefix node@24 2>/dev/null) || prefix=""
    if test -n "$prefix" && test -x "$prefix/bin/node" && test -x "$prefix/bin/npm"; then
      version=$("$prefix/bin/node" -p 'process.versions.node') || version=""
      if test "${version%%.*}" = 24; then
        export PATH="$prefix/bin:$PATH"
        printf 'Using Node.js %s from %s for verification\n' "$version" "$prefix/bin"
        return 0
      fi
    fi
  fi
  bash "$ROOT/scripts/require-node24.sh" || {
    echo 'Install/select Node.js 24 and npm; with Homebrew, run: brew install node@24' >&2
    return 2
  }
}

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

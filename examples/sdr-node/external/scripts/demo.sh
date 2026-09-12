#!/usr/bin/env bash
set -euo pipefail
umask 077

profile_dir=$(cd "$(dirname "$0")/.." && pwd)
example_dir=$(cd "$profile_dir/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
source "$repo_dir/examples/external-ovs-boundary.sh"
graphx=${GRAPHX_BIN:-$repo_dir/build/dev/graphx}
config=$profile_dir/graphx.yaml
compose=$profile_dir/compose.yaml
state=$profile_dir/.state/external.env
namespace=gx-sdr-device
GRAPHX_HOST_UID=$(id -u)
GRAPHX_HOST_GID=$(id -g)
export GRAPHX_HOST_UID GRAPHX_HOST_GID

usage() {
  echo "usage: $0 <up|verify|status|down>" >&2
}
require() {
  command -v "$1" >/dev/null || { echo "missing prerequisite: $1" >&2; return 2; }
}

load_state() {
  test -r "$state"
  # shellcheck disable=SC1090
  source "$state"
  test -n "${GRAPHX_EXTERNAL_OWNER:-}" && test -n "${GRAPHX_SDR_TLS_DIR:-}"
  case "$GRAPHX_SDR_TLS_DIR" in "$profile_dir"/.state/sdr-tls) ;; *) return 2 ;; esac
  export GRAPHX_SDR_TLS_DIR
}
save_state() {
  install -d -m 0700 "$(dirname "$state")"
  GRAPHX_EXTERNAL_OWNER=$(openssl rand -hex 16)
  GRAPHX_SDR_TLS_DIR=$profile_dir/.state/sdr-tls
  printf 'GRAPHX_EXTERNAL_OWNER=%q\nGRAPHX_SDR_TLS_DIR=%q\n' \
    "$GRAPHX_EXTERNAL_OWNER" "$GRAPHX_SDR_TLS_DIR" >"$state.tmp.$$"
  chmod 0600 "$state.tmp.$$"; mv "$state.tmp.$$" "$state"
  export GRAPHX_SDR_TLS_DIR
}
owned_pid() {
  local pid command
  test -r "$profile_dir/.state/sdr.pid" || return 1
  pid=$(cat "$profile_dir/.state/sdr.pid")
  [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
  command=$(ps -p "$pid" -o command= 2>/dev/null || true)
  test -n "$command" && [[ "$command" == *sdr_simulator.py* ]]
}
stop_sdr() {
  local pid
  owned_pid || return 0
  pid=$(cat "$profile_dir/.state/sdr.pid")
  sudo kill "$pid" 2>/dev/null || true
  for _ in {1..30}; do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
  kill -0 "$pid" 2>/dev/null && sudo kill -KILL "$pid" 2>/dev/null || true
  rm -f "$profile_dir/.state/sdr.pid"
}
cleanup_external() {
  sudo bash -c "source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_delete '$GRAPHX_EXTERNAL_OWNER' '$namespace' sdr-dev-ovs br-sdr"
}
start_sdr() {
  # shellcheck disable=SC2024
  sudo sh -c 'echo $$ > "$1"; chown "$5:$6" "$1"; chmod 0600 "$1"; exec ip netns exec gx-sdr-device env PYTHONPATH="$2/common" SDR_SAMPLE_TARGET=10.63.0.20 SDR_TLS_CERT="$3/sdr-node.pem" SDR_TLS_KEY="$3/sdr-node.key" SDR_TLS_CLIENT_CA="$3/ca.pem" python3 "$2/common/sdr_simulator.py"' \
    sh "$profile_dir/.state/sdr.pid" "$example_dir" "$GRAPHX_SDR_TLS_DIR" unused \
    "$(id -u)" "$(id -g)" >"$profile_dir/.state/sdr.log" 2>&1 &
  for _ in {1..50}; do owned_pid && return; sleep 0.1; done
  echo "SDR simulator failed to start" >&2; return 1
}
verify_sdr() {
  load_state
  local running ready=false
  running=$(sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
    docker compose -f "$compose" ps --status running --services)
  for service in processor sink; do
    grep -qx "$service" <<<"$running" || {
      echo "external SDR service is not running: $service" >&2
      return 1
    }
  done
  owned_pid || { echo "external SDR simulator is not running" >&2; return 1; }
  sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
    docker compose -f "$compose" exec -T processor python3 common/sdrctl.py status \
    | grep -q '"accepted": true'
  for _ in {1..30}; do
    if sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
        docker compose -f "$compose" logs sink | grep -q 'result '; then
      ready=true
      break
    fi
    sleep 0.2
  done
  test "$ready" = true || { echo "external SDR results did not reach the sink" >&2; return 1; }
  echo "external SDR delivery and mutual-TLS control passed"
}
rollback_up() {
  local original=$? cleanup_status=0 core_status=0 compose_status=0
  set +e
  stop_sdr
  cleanup_external || cleanup_status=$?
  sudo "$graphx" infra destroy "$config" || core_status=$?
  sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
    docker compose -f "$compose" down || compose_status=$?
  if test "$cleanup_status" -eq 0 && test "$core_status" -eq 0 && \
     test "$compose_status" -eq 0; then
    rm -f "$state"
  else
    echo "Rollback retained network-lab SDR state for ownership-safe recovery" >&2
  fi
  return "$original"
}

case ${1:-} in
  up)
    test "$(uname -s)" = Linux; test -x "$graphx"; sudo -v
    test ! -e "$state" || { echo "network-lab SDR lab is already owned" >&2; exit 2; }
    save_state
    trap rollback_up ERR
    "$example_dir/common/generate_tls.sh" "$GRAPHX_SDR_TLS_DIR"
    sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
      docker compose -f "$compose" up -d --build
    sudo "$graphx" infra create "$config"
    sudo bash -c "source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_create '$GRAPHX_EXTERNAL_OWNER' '$namespace' sdr-dev-ovs sdr-dev br-sdr 10.63.0.10/24 02:63:00:00:00:10"
    start_sdr
    trap - ERR
    sudo "$graphx" infra status "$config"
    ;;
  status)
    load_state; sudo "$graphx" infra status "$config"
    sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
      docker compose -f "$compose" ps
    owned_pid && echo "external SDR simulator: running"
    ;;
  verify)
    require docker
    verify_sdr
    ;;
  down)
    if test ! -r "$state"; then echo "external SDR profile is already down"; exit 0; fi
    load_state; stop_sdr; cleanup_external
    sudo "$graphx" infra destroy "$config"
    sudo --preserve-env=GRAPHX_SDR_TLS_DIR,GRAPHX_HOST_UID,GRAPHX_HOST_GID \
      docker compose -f "$compose" down
    rm -f "$state"
    ;;
  *) usage; exit 64 ;;
esac

#!/usr/bin/env bash
set -euo pipefail
umask 077

example_dir=$(cd "$(dirname "$0")/.." && pwd)
repo_dir=$(cd "$example_dir/../.." && pwd)
source "$repo_dir/examples/external-ovs-boundary.sh"
graphx=${GRAPHX_BIN:-$repo_dir/build/dev/graphx}
config=$example_dir/graphx.yaml
state=$example_dir/.state/m5-external.env

load_state() { test -r "$state"; source "$state"; test -n "${GRAPHX_M5_EXTERNAL_OWNER:-}"; }
save_state() {
  install -d -m 0700 "$(dirname "$state")"
  GRAPHX_M5_EXTERNAL_OWNER=$(openssl rand -hex 16)
  printf 'GRAPHX_M5_EXTERNAL_OWNER=%q\n' "$GRAPHX_M5_EXTERNAL_OWNER" >"$state.tmp.$$"
  chmod 0600 "$state.tmp.$$"; mv "$state.tmp.$$" "$state"
}
cleanup_external() {
  sudo bash -c "set -e; source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_delete '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-left-end rtl-ovs br-route-left; graphx_external_namespace_delete '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-middle-end rtm-ovs br-route-middle; graphx_external_namespace_delete '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-right-end rtr-ovs br-route-right"
}
rollback_up() {
  local original=$? cleanup_status=0 core_status=0
  set +e
  cleanup_external || cleanup_status=$?
  sudo "$graphx" infra destroy "$config" || core_status=$?
  if test "$cleanup_status" -eq 0 && test "$core_status" -eq 0; then
    rm -f "$state"
  else
    echo "Rollback retained M5 route-lab state for ownership-safe recovery" >&2
  fi
  return "$original"
}

case ${1:-} in
  up)
    test "$(uname -s)" = Linux; test -x "$graphx"; sudo -v
    test ! -e "$state" || { echo "M5 route lab is already owned" >&2; exit 2; }
    save_state
    trap rollback_up ERR
    sudo "$graphx" infra create "$config"
    sudo bash -c "source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_create '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-left-end rtl-ovs rtl-end br-route-left 10.64.1.10/24 02:64:00:00:01:10 10.64.2.0/24 10.64.1.1 10.64.30.10/32 10.64.1.1"
    sudo bash -c "source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_create '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-middle-end rtm-ovs rtm-end br-route-middle 10.64.2.10/24 02:64:00:00:02:10 10.64.1.0/24 10.64.2.1"
    sudo bash -c "source '$repo_dir/examples/external-ovs-boundary.sh'; graphx_external_namespace_create '$GRAPHX_M5_EXTERNAL_OWNER' gx-route-right-end rtr-ovs rtr-end br-route-right 10.64.3.10/24 02:64:00:00:03:10 10.64.1.0/24 10.64.3.1"
    sudo ip netns exec gx-route-right-end ip address replace 10.64.30.10/32 dev lo
    trap - ERR
    sudo "$graphx" infra status "$config"
    ;;
  status)
    load_state; sudo "$graphx" infra status "$config"
    sudo ip netns list | grep -E '^gx-route-(left|middle|right)-end'
    ;;
  apply-route)
    load_state
    sudo "$graphx" infra route apply "$config" --router route-router \
      --destination 10.64.30.10/32
    ;;
  clear-route)
    load_state
    sudo "$graphx" infra route clear "$config" --router route-router \
      --destination 10.64.30.10/32
    ;;
  down)
    load_state
    cleanup_external
    sudo "$graphx" infra destroy "$config"
    rm -f "$state"
    ;;
  *) echo "usage: $0 <up|status|apply-route|clear-route|down>" >&2; exit 64 ;;
esac

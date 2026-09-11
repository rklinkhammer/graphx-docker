#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "$0")" && pwd)
tap_lab=$script_dir/../tap/scripts/ovs-lab.sh

case ${1:-} in
  start) shift; exec "$tap_lab" up "$@" ;;
  stop) shift; exec "$tap_lab" down "$@" ;;
  status|verify|pause|resume)
    action=$1
    shift
    exec "$tap_lab" "$action" "$@"
    ;;
  *)
    echo "usage: $0 <start|status|verify|pause|resume|stop>" >&2
    exit 64
    ;;
esac

#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
repo_dir=$(CDPATH='' cd -- "${script_dir}/../../.." && pwd -P)
tap_lab=${script_dir}/../tap/scripts/ovs-lab.sh
action=${1:-}
if [[ -n ${action} ]]; then shift; fi

case ${action} in
  start) tap_action=up ;;
  stop) tap_action=down ;;
  status|verify|pause|resume) tap_action=${action} ;;
  *)
    echo "usage: $0 <start|status|verify|pause|resume|stop>" >&2
    exit 64
    ;;
esac

case $(uname -s) in
  Linux)
    exec "${tap_lab}" "${tap_action}" "$@"
    ;;
  Darwin)
    # shellcheck source=../../../infrastructure/lima/common.sh
    source "${repo_dir}/infrastructure/lima/common.sh"
    graphx_lima_require_host
    config_digest=$(graphx_lima_digest)
    instance_status=$(graphx_lima_assert_identity "${config_digest}")
    if [[ ${instance_status} != Running ]]; then
      echo "GraphX Lima instance '${GRAPHX_LIMA_INSTANCE}' is ${instance_status}; run infrastructure/lima/start.sh." >&2
      exit 2
    fi

    guest_graphx=${GRAPHX_LIMA_GRAPHX_BIN:-/var/lib/graphx/runtime/build/dev/graphx}
    # shellcheck disable=SC2016
    "${GRAPHX_LIMA_RUNNER}" 120 limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" \
      "${GRAPHX_LIMA_INSTANCE}" -- bash -c \
      'test -x "$1" && test -s examples/qemu-node/output/images/bzImage &&
       test -s examples/qemu-node/output/images/rootfs.cpio.gz &&
       command -v qemu-system-x86_64 >/dev/null &&
       systemctl is-active --quiet openvswitch-switch.service' \
      _ "${guest_graphx}" || {
        echo "The Lima QEMU/TAP prerequisites, guest image, or Linux GraphX CLI are unavailable." >&2
        echo "Run infrastructure/lima/start.sh, infrastructure/lima/verify.sh, and examples/qemu-node/scripts/build.sh first." >&2
        exit 2
      }

    case ${tap_action} in
      up|down) deadline=1800 ;;
      *) deadline=180 ;;
    esac
    exec "${GRAPHX_LIMA_RUNNER}" "${deadline}" \
      limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" "${GRAPHX_LIMA_INSTANCE}" -- \
      env GRAPHX_BIN="${guest_graphx}" examples/qemu-node/scripts/demo.sh "${action}" "$@"
    ;;
  *)
    echo "The canonical QEMU TAP/OVS demo requires Linux or Apple Silicon macOS with GraphX Lima." >&2
    exit 2
    ;;
esac

#!/usr/bin/env bash
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)

usage() {
  cat <<'EOF'
usage: scripts/network-lab.sh <macvlan|ipvlan-l2|ipvlan-l3|mixed-network> <plan|up|status|down>

Runs the selected system-OVS laboratory directly on Linux or inside the
identity-checked GraphX Lima VM on Apple Silicon macOS.
EOF
}

if [[ $# -ne 2 ]]; then
  usage >&2
  exit 64
fi

lab=$1
action=$2
case ${lab} in
  macvlan|ipvlan-l2|ipvlan-l3|mixed-network) ;;
  *) echo "unsupported network laboratory: ${lab}" >&2; usage >&2; exit 64 ;;
esac
case ${action} in
  plan|up|status|down) ;;
  *) echo "unsupported network laboratory action: ${action}" >&2; usage >&2; exit 64 ;;
esac

run_linux() {
  local graphx=${GRAPHX_BIN:-${ROOT}/build/dev/graphx}
  local config=${ROOT}/examples/${lab}/graphx.yaml
  if [[ ! -x ${graphx} ]]; then
    echo "GraphX Linux CLI not found at ${graphx}." >&2
    echo "Build it on this Linux system or set GRAPHX_BIN to its absolute path." >&2
    return 2
  fi
  if [[ ${action} == plan ]]; then
    exec "${graphx}" infra create "${config}" --dry-run
  fi
  exec "${ROOT}/examples/${lab}/scripts/${action}.sh"
}

case $(uname -s) in
  Linux)
    run_linux
    ;;
  Darwin)
    # shellcheck source=../infrastructure/lima/common.sh
    source "${ROOT}/infrastructure/lima/common.sh"
    graphx_m1_require_host
    config_digest=$(graphx_m1_digest)
    instance_status=$(graphx_m1_assert_identity "${config_digest}")
    if [[ ${instance_status} != Running ]]; then
      echo "GraphX Lima instance '${GRAPHX_M1_INSTANCE}' is ${instance_status}; run infrastructure/lima/start.sh." >&2
      exit 2
    fi

    guest_graphx=${GRAPHX_LIMA_GRAPHX_BIN:-/var/lib/graphx/m1/build/dev/graphx}
    "${GRAPHX_M1_RUNNER}" 120 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
      "${GRAPHX_M1_INSTANCE}" -- bash -c \
      'test -x "$1" && docker info >/dev/null && systemctl is-active --quiet openvswitch-switch.service' \
      _ "${guest_graphx}" || {
        echo "The Lima network-lab prerequisites or Linux GraphX CLI are unavailable." >&2
        echo "Run infrastructure/lima/start.sh and infrastructure/lima/verify.sh first." >&2
        exit 2
      }

    case ${action} in
      plan|status) deadline=120 ;;
      up|down) deadline=1800 ;;
    esac
    exec "${GRAPHX_M1_RUNNER}" "${deadline}" \
      limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" "${GRAPHX_M1_INSTANCE}" -- \
      env GRAPHX_BIN="${guest_graphx}" scripts/network-lab.sh "${lab}" "${action}"
    ;;
  *)
    echo "system-OVS network laboratories require Linux or Apple Silicon macOS with GraphX Lima." >&2
    exit 2
    ;;
esac

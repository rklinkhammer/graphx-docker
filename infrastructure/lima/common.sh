#!/usr/bin/env bash
set -euo pipefail

GRAPHX_LIMA_INSTANCE=graphx
GRAPHX_LIMA_GUEST_ROOT=/workspace/graphx-docker
GRAPHX_LIMA_STATE_ROOT=/var/lib/graphx
GRAPHX_LIMA_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
GRAPHX_LIMA_REPO_ROOT=$(CDPATH= cd -- "${GRAPHX_LIMA_SCRIPT_DIR}/../.." && pwd -P)
GRAPHX_LIMA_RUNNER="${GRAPHX_LIMA_SCRIPT_DIR}/run-bounded.py"

graphx_lima_digest() {
  local path
  for path in graphx.yaml provision.sh common.sh run-bounded.py start.sh stop.sh verify.sh; do
    shasum -a 256 "${GRAPHX_LIMA_SCRIPT_DIR}/${path}"
  done | shasum -a 256 | awk '{print $1}'
}

graphx_lima_instance_record() {
  limactl list "${GRAPHX_LIMA_INSTANCE}" \
    --format '{{.Name}}|{{.Status}}|{{.Arch}}|{{.VMType}}|{{index .Param "repo"}}|{{index .Param "configDigest"}}' \
    2>/dev/null || true
}

graphx_lima_require_host() {
  [[ $(uname -s) == Darwin ]] || {
    echo "GraphX Lima host lifecycle scripts require macOS." >&2
    return 1
  }
  [[ $(uname -m) == arm64 ]] || {
    echo "GraphX Lima requires Apple Silicon (arm64)." >&2
    return 1
  }
  command -v limactl >/dev/null || {
    echo "Install Lima 2.2.0 or newer before continuing." >&2
    return 1
  }
  command -v python3 >/dev/null || {
    echo "python3 is required for bounded lifecycle commands." >&2
    return 1
  }
}

graphx_lima_assert_identity() {
  local expected_digest=$1 record name status arch vm_type repo digest
  record=$(graphx_lima_instance_record)
  [[ -n ${record} ]] || {
    echo "Lima instance '${GRAPHX_LIMA_INSTANCE}' does not exist." >&2
    return 1
  }
  IFS='|' read -r name status arch vm_type repo digest <<<"${record}"
  [[ ${name} == "${GRAPHX_LIMA_INSTANCE}" && ${arch} == aarch64 && ${vm_type} == vz ]] || {
    echo "Refusing Lima instance '${name}': expected graphx/aarch64/vz, got ${name}/${arch}/${vm_type}." >&2
    return 1
  }
  [[ ${repo} == "${GRAPHX_LIMA_REPO_ROOT}" ]] || {
    echo "Refusing instance with unexpected source mount parameter: ${repo}" >&2
    return 1
  }
  [[ ${digest} == "${expected_digest}" ]] || {
    echo "Refusing stale GraphX instance configuration (${digest:-missing}); expected ${expected_digest}." >&2
    echo "Use the documented deliberate removal procedure, then recreate it." >&2
    return 1
  }
  printf '%s\n' "${status}"
}

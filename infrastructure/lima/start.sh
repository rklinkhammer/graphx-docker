#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=common.sh
source "${script_dir}/common.sh"

graphx_m1_require_host
limactl validate "${script_dir}/graphx.yaml"
config_digest=$(graphx_m1_digest)
record=$(graphx_m1_instance_record)

if [[ -z ${record} ]]; then
  echo "Creating bounded Lima instance '${GRAPHX_M1_INSTANCE}'..."
  "${GRAPHX_M1_RUNNER}" 1800 limactl create --tty=false \
    --name "${GRAPHX_M1_INSTANCE}" \
    --param "repo=${GRAPHX_M1_REPO_ROOT}" \
    --param "configDigest=${config_digest}" \
    "${script_dir}/graphx.yaml"
else
  graphx_m1_assert_identity "${config_digest}" >/dev/null
fi

status=$(graphx_m1_assert_identity "${config_digest}")
case ${status} in
  Running)
    echo "Lima instance '${GRAPHX_M1_INSTANCE}' is already running."
    ;;
  Stopped)
    echo "Starting Lima instance '${GRAPHX_M1_INSTANCE}'..."
    "${GRAPHX_M1_RUNNER}" 1800 limactl start "${GRAPHX_M1_INSTANCE}"
    ;;
  *)
    echo "Instance is in unsupported state '${status}'; wait for it to settle or inspect limactl list." >&2
    exit 1
    ;;
esac

"${GRAPHX_M1_RUNNER}" 120 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
  "${GRAPHX_M1_INSTANCE}" -- \
  sudo bash -c 'test "$1" = "$(cat /etc/graphx-m1-config.sha256)"' _ "${config_digest}"

# Lima establishes its initial SSH control session before system provisioning
# adds the login user to the docker group. Refresh the VM once when that session
# still has the pre-provisioning supplementary groups, then fail closed if the
# documented non-sudo Docker workflow is not available.
if ! "${GRAPHX_M1_RUNNER}" 120 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
  "${GRAPHX_M1_INSTANCE}" -- docker info >/dev/null 2>&1; then
  echo "Refreshing the Lima login session for Docker group membership..."
  "${GRAPHX_M1_RUNNER}" 300 limactl stop "${GRAPHX_M1_INSTANCE}"
  "${GRAPHX_M1_RUNNER}" 1800 limactl start "${GRAPHX_M1_INSTANCE}"
fi
"${GRAPHX_M1_RUNNER}" 120 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
  "${GRAPHX_M1_INSTANCE}" -- bash -c \
  'docker info >/dev/null && docker buildx version >/dev/null'
echo "GraphX M1 is running. Verify it with ${script_dir}/verify.sh"

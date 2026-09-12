#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=common.sh
source "${script_dir}/common.sh"

graphx_lima_require_host
limactl validate "${script_dir}/graphx.yaml"
config_digest=$(graphx_lima_digest)
record=$(graphx_lima_instance_record)

if [[ -z ${record} ]]; then
  echo "Creating bounded Lima instance '${GRAPHX_LIMA_INSTANCE}'..."
  "${GRAPHX_LIMA_RUNNER}" 1800 limactl create --tty=false \
    --name "${GRAPHX_LIMA_INSTANCE}" \
    --param "repo=${GRAPHX_LIMA_REPO_ROOT}" \
    --param "configDigest=${config_digest}" \
    "${script_dir}/graphx.yaml"
else
  graphx_lima_assert_identity "${config_digest}" >/dev/null
fi

status=$(graphx_lima_assert_identity "${config_digest}")
case ${status} in
  Running)
    echo "Lima instance '${GRAPHX_LIMA_INSTANCE}' is already running."
    ;;
  Stopped)
    echo "Starting Lima instance '${GRAPHX_LIMA_INSTANCE}'..."
    "${GRAPHX_LIMA_RUNNER}" 1800 limactl start "${GRAPHX_LIMA_INSTANCE}"
    ;;
  *)
    echo "Instance is in unsupported state '${status}'; wait for it to settle or inspect limactl list." >&2
    exit 1
    ;;
esac

"${GRAPHX_LIMA_RUNNER}" 120 limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" \
  "${GRAPHX_LIMA_INSTANCE}" -- \
  sudo bash -c 'test "$1" = "$(cat /etc/graphx-lima-config.sha256)"' _ "${config_digest}"

# Lima establishes its initial SSH control session before system provisioning
# adds the login user to the docker group. Refresh the VM once when that session
# still has the pre-provisioning supplementary groups, then fail closed if the
# documented non-sudo Docker workflow is not available.
if ! "${GRAPHX_LIMA_RUNNER}" 120 limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" \
  "${GRAPHX_LIMA_INSTANCE}" -- docker info >/dev/null 2>&1; then
  echo "Refreshing the Lima login session for Docker group membership..."
  "${GRAPHX_LIMA_RUNNER}" 300 limactl stop "${GRAPHX_LIMA_INSTANCE}"
  "${GRAPHX_LIMA_RUNNER}" 1800 limactl start "${GRAPHX_LIMA_INSTANCE}"
fi
"${GRAPHX_LIMA_RUNNER}" 120 limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" \
  "${GRAPHX_LIMA_INSTANCE}" -- bash -c \
  'docker info >/dev/null && docker buildx version >/dev/null &&
   node --version | grep -Eq "^v24\\." && node -e "import(\"node:sqlite\")"'
echo "GraphX Lima is running. Verify it with ${script_dir}/verify.sh"

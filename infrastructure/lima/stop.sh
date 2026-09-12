#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
# shellcheck source=common.sh
source "${script_dir}/common.sh"

graphx_lima_require_host
config_digest=$(graphx_lima_digest)
record=$(graphx_lima_instance_record)
if [[ -z ${record} ]]; then
  echo "Lima instance '${GRAPHX_LIMA_INSTANCE}' is absent; nothing to stop."
  exit 0
fi

status=$(graphx_lima_assert_identity "${config_digest}")
case ${status} in
  Running)
    "${GRAPHX_LIMA_RUNNER}" 180 limactl stop "${GRAPHX_LIMA_INSTANCE}"
    echo "Stopped Lima instance '${GRAPHX_LIMA_INSTANCE}'; VM-local evidence was retained."
    ;;
  Stopped)
    echo "Lima instance '${GRAPHX_LIMA_INSTANCE}' is already stopped."
    ;;
  *)
    echo "Refusing to stop instance in unexpected state '${status}'." >&2
    exit 1
    ;;
esac

#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

if [[ $(uname -s) == Darwin ]]; then
  # shellcheck source=common.sh
  source "${script_dir}/common.sh"
  graphx_lima_require_host
  config_digest=$(graphx_lima_digest)
  status=$(graphx_lima_assert_identity "${config_digest}")
  [[ ${status} == Running ]] || {
    echo "GraphX Lima is not running; run ${script_dir}/start.sh first." >&2
    exit 1
  }
  exec "${GRAPHX_LIMA_RUNNER}" 3600 limactl shell --workdir "${GRAPHX_LIMA_GUEST_ROOT}" \
    "${GRAPHX_LIMA_INSTANCE}" -- sudo env GRAPHX_LIMA_CONFIG_DIGEST="${config_digest}" \
    bash "${GRAPHX_LIMA_GUEST_ROOT}/infrastructure/lima/verify.sh" --guest
fi

[[ ${1:-} == --guest && $(uname -s) == Linux && ${EUID} -eq 0 ]] || {
  echo "Run verify.sh on macOS, or as root in the Lima guest with --guest." >&2
  exit 1
}

readonly source_root=/workspace/graphx-docker
readonly runtime_root=/var/lib/graphx/runtime
readonly build_dir=${runtime_root}/build/dev
readonly evidence_root=${runtime_root}/evidence
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
readonly evidence_dir=${evidence_root}/${timestamp}

fail() { echo "Lima verification failed: $*" >&2; exit 1; }
require() { command -v "$1" >/dev/null || fail "required command is missing: $1"; }
snapshot() {
  local label=$1 directory=${evidence_dir}/snapshot-${1}
  install -d -m 0750 "${directory}"
  ip -j link show | jq -S 'map({ifname, link_type, operstate})' >"${directory}/links.json"
  ovs-vsctl list-br | sort >"${directory}/ovs-bridges.txt"
  ip netns list | sort >"${directory}/namespaces.txt"
  docker ps --format '{{.Names}}' | sort >"${directory}/containers.txt"
  nft list tables | sort >"${directory}/nft-tables.txt"
  echo "snapshot ${label}" >"${directory}/result.txt"
}

for command in docker ovs-vsctl ip tc nft dumpcap capinfos tshark qemu-system-x86_64 \
  cmake ninja jq openssl node npm; do
  require "${command}"
done
[[ $(node --version) =~ ^v24\. ]] || fail "Node.js 24 is required"
timeout 30 node -e 'import("node:sqlite")' >/dev/null || fail "Node.js node:sqlite is unavailable"
[[ $(uname -m) == aarch64 ]] || fail "guest architecture is not aarch64"
[[ $(cat /etc/graphx-lima-config.sha256) == "${GRAPHX_LIMA_CONFIG_DIGEST:?missing expected digest}" ]] || \
  fail "guest configuration identity does not match"

login_user=${GRAPHX_LIMA_USER:-${SUDO_USER:-}}
[[ -n ${login_user} ]] || login_user=$(stat -c %U "${source_root}")
[[ ${login_user} != root ]] || fail "Lima verification must be entered through the login user"
id -nG "${login_user}" | tr ' ' '\n' | grep -Fxq docker || fail "Lima login user lacks Docker access"
sudo -u "${login_user}" docker info >/dev/null || fail "Lima login user cannot access rootful Docker"
mountpoint -q "${source_root}" || fail "source checkout is not mounted"
source_type=$(findmnt -n -o FSTYPE -T "${source_root}")
state_type=$(findmnt -n -o FSTYPE -T /var/lib/graphx)
[[ ${source_type} == virtiofs && ${state_type} != virtiofs ]] || \
  fail "runtime state is not isolated from the shared virtiofs mount"
for path in /var/lib/docker /var/lib/openvswitch /var/lib/graphx/qemu /var/lib/graphx/captures; do
  [[ $(findmnt -n -o SOURCE -T "${path}") == "$(findmnt -n -o SOURCE -T /var/lib/graphx)" ]] || \
    fail "${path} is not VM-local"
done
systemctl is-active --quiet docker.service || fail "rootful Docker system service is not active"
systemctl is-active --quiet openvswitch-switch.service || fail "Open vSwitch system service is not active"
[[ $(docker info --format '{{.DockerRootDir}}') == /var/lib/docker ]] || fail "Docker data root is unexpected"
docker compose version >/dev/null
docker buildx version >/dev/null

install -d -m 0750 "${evidence_dir}"
snapshot before
cd "${source_root}"
GRAPHX_DEV_BUILD_DIR="${build_dir}" GRAPHX_VERIFY_LOG_DIR="${evidence_dir}/verification-log" \
  timeout 1800 scripts/verify.sh quick >"${evidence_dir}/graphx-quick.txt" 2>&1
cmake -S . -B "${build_dir}" -G Ninja -DGRAPHX_BUILD_TESTS=ON \
  -DGRAPHX_ENABLE_LINUX_OVS_TESTS=ON >"${evidence_dir}/privileged-configure.txt"
cmake --build "${build_dir}" -j "${GRAPHX_BUILD_JOBS:-4}" >"${evidence_dir}/privileged-build.txt"
ctest --test-dir "${build_dir}" -L privileged --output-on-failure \
  >"${evidence_dir}/privileged-tests.txt"
snapshot after
for name in links.json ovs-bridges.txt namespaces.txt containers.txt nft-tables.txt; do
  diff -u "${evidence_dir}/snapshot-before/${name}" "${evidence_dir}/snapshot-after/${name}" \
    >>"${evidence_dir}/snapshot.diff" || fail "production lifecycle tests left infrastructure behind"
done

{
  echo "status=passed"
  echo "timestamp=${timestamp}"
  echo "instance=graphx"
  echo "guest_arch=$(uname -m)"
  echo "kernel=$(uname -sr)"
  echo "source_fstype=${source_type}"
  echo "state_fstype=${state_type}"
  echo "docker=$(docker version --format '{{.Server.Version}}')"
  echo "ovs=$(ovs-vsctl --version | head -n 1)"
} >"${evidence_dir}/result.txt"

echo "GraphX Lima verification passed; evidence: ${evidence_dir}"

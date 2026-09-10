#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

if [[ $(uname -s) == Darwin ]]; then
  # shellcheck source=common.sh
  source "${script_dir}/common.sh"
  graphx_m1_require_host
  config_digest=$(graphx_m1_digest)
  status=$(graphx_m1_assert_identity "${config_digest}")
  [[ ${status} == Running ]] || {
    echo "GraphX M1 is not running; run ${script_dir}/start.sh first." >&2
    exit 1
  }
  exec "${GRAPHX_M1_RUNNER}" 3600 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
    "${GRAPHX_M1_INSTANCE}" -- sudo env GRAPHX_M1_CONFIG_DIGEST="${config_digest}" \
    bash "${GRAPHX_M1_GUEST_ROOT}/infrastructure/lima/verify.sh" --guest
fi

[[ ${1:-} == --guest && $(uname -s) == Linux && ${EUID} -eq 0 ]] || {
  echo "Run verify.sh on macOS, or as root in the Lima guest with --guest." >&2
  exit 1
}

readonly bridge=gx-m1-br
readonly namespace=gx-m1-ns
readonly veth_host=gx-m1-vh
readonly veth_ns=gx-m1-vn
readonly tap=gx-m1-tap
readonly internal=gx-m1-int
readonly container=gx-m1-docker
readonly nft_table=gx_m1
readonly state_dir=/run/graphx-m1
readonly evidence_root=/var/lib/graphx/m1/evidence
readonly evidence_count_limit=10
readonly evidence_kib_limit=262144
token=$(openssl rand -hex 16)
readonly token
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
readonly timestamp
readonly evidence_dir="${evidence_root}/${timestamp}-${token:0:8}"
capture_pid=
cleanup_done=0

fail() {
  echo "GraphX M1 verification failed: $*" >&2
  return 1
}

maybe_fail() {
  [[ ${GRAPHX_M1_TEST_FAIL_AFTER:-} != "$1" ]] || fail "injected failure after $1"
}

remove_evidence_dir() {
  local target=$1 name
  name=$(basename -- "${target}")
  [[ ${target} == "${evidence_root}/${name}" && ${name} =~ ^[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$ ]] || {
    echo "Refusing unsafe evidence removal: ${target}" >&2
    return 1
  }
  [[ ${target} != "${evidence_dir}" ]] || return 0
  [[ ! -d ${target} || -L ${target} ]] && return 0
  find -P "${target}" -mindepth 1 -type f -delete
  find -P "${target}" -mindepth 1 -type l -delete
  find -P "${target}" -mindepth 1 -depth -type d -exec rmdir -- {} \;
  rmdir -- "${target}"
}

rotate_evidence() {
  local limit=${1:-${evidence_count_limit}} total=0 count candidate size
  local -a directories=()
  install -d -m 0750 "${evidence_root}"
  mapfile -t directories < <(
    find -P "${evidence_root}" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' \
      | grep -E '^[0-9]{8}T[0-9]{6}Z-[0-9a-f]{8}$' | sort || true
  )
  count=${#directories[@]}
  for candidate in "${directories[@]}"; do
    size=$(du -sk -- "${evidence_root}/${candidate}" | awk '{print $1}')
    total=$((total + size))
  done
  for candidate in "${directories[@]}"; do
    (( count > limit || total > evidence_kib_limit )) || break
    [[ ${evidence_root}/${candidate} != "${evidence_dir}" ]] || continue
    size=$(du -sk -- "${evidence_root}/${candidate}" | awk '{print $1}')
    remove_evidence_dir "${evidence_root}/${candidate}"
    total=$((total - size))
    count=$((count - 1))
  done
}

state_value() {
  local key=$1
  [[ -f ${state_dir}/${key} ]] && cat "${state_dir}/${key}"
}

cleanup_issue() {
  local message=$1
  echo "${message}" >&2
  if [[ -d ${evidence_dir} && ! -L ${evidence_dir} ]]; then
    printf '%s\n' "${message}" >>"${evidence_dir}/cleanup-errors.txt" || true
  fi
}

link_matches() {
  local name=$1 expected=$2 actual
  [[ -e /sys/class/net/${name}/ifindex ]] || return 1
  actual=$(cat "/sys/class/net/${name}/ifindex")
  [[ ${actual} == "${expected}" ]]
}

cleanup() {
  (
    trap '' INT TERM HUP
    local failed=0 expected current name namespace_safe_to_delete=1
    local -a namespace_links=()

  if [[ -n ${capture_pid} && -d /proc/${capture_pid} ]]; then
    if tr '\0' ' ' <"/proc/${capture_pid}/cmdline" | grep -Fq "${evidence_dir}/packet.pcap"; then
      kill "${capture_pid}" 2>/dev/null || true
      if ! timeout 5 bash -c 'while kill -0 "$1" 2>/dev/null; do sleep 0.1; done' _ "${capture_pid}"; then
        kill -KILL "${capture_pid}" 2>/dev/null || true
      fi
      wait "${capture_pid}" 2>/dev/null || true
    else
      cleanup_issue "Refusing to signal capture PID ${capture_pid}: identity changed."
      failed=1
    fi
    capture_pid=
  fi

  if docker inspect "${container}" >/dev/null 2>&1; then
    if [[ $(docker inspect -f '{{index .Config.Labels "graphx.m1.owner"}}' "${container}" 2>/dev/null) == "${token}" ]]; then
      timeout 15 docker rm -f "${container}" >/dev/null || failed=1
    else
      cleanup_issue "Refusing to remove container ${container}: owner mismatch."
      failed=1
    fi
  fi

  if ip netns list | awk '{print $1}' | grep -Fxq "${namespace}"; then
    expected=$(state_value veth-ns.ifindex || true)
    if ip netns exec "${namespace}" ip link show dev "${veth_ns}" >/dev/null 2>&1; then
      current=$(ip netns exec "${namespace}" cat "/sys/class/net/${veth_ns}/ifindex" 2>/dev/null || true)
      if [[ -z ${expected} || ${current} != "${expected}" ]]; then
        cleanup_issue "Refusing namespace cleanup: link ${veth_ns} expected ifindex ${expected:-missing}, found ${current:-missing}."
        namespace_safe_to_delete=0
        failed=1
      fi
    fi
    mapfile -t namespace_links < <(
      ip netns exec "${namespace}" ip -o link show | awk -F': ' '{name=$2; sub(/@.*/, "", name); print name}'
    )
    for name in "${namespace_links[@]}"; do
      if [[ ${name} != lo && ${name} != "${veth_ns}" ]]; then
        cleanup_issue "Refusing namespace cleanup: unexpected link ${name} is present in ${namespace}."
        namespace_safe_to_delete=0
        failed=1
      fi
    done
  fi

  expected=$(state_value veth-host.ifindex || true)
  if ip link show dev "${veth_host}" >/dev/null 2>&1; then
    if [[ -n ${expected} ]] && link_matches "${veth_host}" "${expected}"; then
      ip link delete "${veth_host}" || failed=1
    else
      current=$(cat "/sys/class/net/${veth_host}/ifindex" 2>/dev/null || true)
      cleanup_issue "Refusing to remove link ${veth_host}: expected ifindex ${expected:-missing}, found ${current:-missing}."
      failed=1
    fi
  fi

  expected=$(state_value tap.ifindex || true)
  if ip link show dev "${tap}" >/dev/null 2>&1; then
    if [[ -n ${expected} ]] && link_matches "${tap}" "${expected}"; then
      ip link delete "${tap}" || failed=1
    else
      current=$(cat "/sys/class/net/${tap}/ifindex" 2>/dev/null || true)
      cleanup_issue "Refusing to remove link ${tap}: expected ifindex ${expected:-missing}, found ${current:-missing}."
      failed=1
    fi
  fi

  expected=$(state_value internal.ifindex || true)
  if ip link show dev "${internal}" >/dev/null 2>&1; then
    if ! [[ -n ${expected} ]] || ! link_matches "${internal}" "${expected}"; then
      current=$(cat "/sys/class/net/${internal}/ifindex" 2>/dev/null || true)
      cleanup_issue "Internal link ${internal} identity changed: expected ifindex ${expected:-missing}, found ${current:-missing}."
      failed=1
    fi
  fi

  if ip netns list | awk '{print $1}' | grep -Fxq "${namespace}"; then
    expected=$(state_value namespace.inode || true)
    current=$(stat -Lc '%i' "/run/netns/${namespace}" 2>/dev/null || true)
    if [[ -n ${expected} && ${current} == "${expected}" && ${namespace_safe_to_delete} -eq 1 ]]; then
      ip netns delete "${namespace}" || failed=1
    elif [[ ${namespace_safe_to_delete} -eq 0 ]]; then
      cleanup_issue "Preserving namespace ${namespace} because its contents changed."
    else
      cleanup_issue "Refusing to remove namespace ${namespace}: expected inode ${expected:-missing}, found ${current:-missing}."
      failed=1
    fi
  fi

  if ovs-vsctl br-exists "${bridge}" >/dev/null 2>&1; then
    expected=$(state_value bridge.uuid || true)
    current=$(ovs-vsctl --if-exists get Bridge "${bridge}" _uuid 2>/dev/null | tr -d '"')
    if [[ -n ${expected} && ${current} == "${expected}" && \
      $(ovs-vsctl --if-exists get Bridge "${bridge}" external_ids:graphx_m1_owner 2>/dev/null | tr -d '"') == "${token}" ]]; then
      ovs-vsctl --timeout=10 --if-exists del-br "${bridge}" || failed=1
    else
      cleanup_issue "Refusing to remove bridge ${bridge}: expected UUID ${expected:-missing}, found ${current:-missing}."
      failed=1
    fi
  fi

  if [[ -e ${state_dir} ]]; then
    if [[ -d ${state_dir} && ! -L ${state_dir} && $(state_value owner || true) == "${token}" ]]; then
      for expected in owner bridge.uuid internal.ifindex namespace.inode veth-host.ifindex veth-ns.ifindex tap.ifindex; do
        [[ ! -e ${state_dir}/${expected} || -f ${state_dir}/${expected} ]] || {
          cleanup_issue "Refusing unexpected state object: ${state_dir}/${expected}"
          failed=1
          continue
        }
        rm -f -- "${state_dir}/${expected}" || failed=1
      done
      if ! rmdir -- "${state_dir}" 2>/dev/null; then
        cleanup_issue "Refusing to remove non-empty verifier state directory ${state_dir}."
        failed=1
      fi
    else
      cleanup_issue "Refusing to remove verifier state directory: owner mismatch."
      failed=1
    fi
  fi
    exit "${failed}"
  )
}

assert_clean() {
  local failed=0 name
  if docker inspect "${container}" >/dev/null 2>&1; then
    echo "Disposable container remains: ${container}" >&2
    failed=1
  fi
  for name in "${bridge}" "${veth_host}" "${veth_ns}" "${tap}" "${internal}"; do
    if ip link show dev "${name}" >/dev/null 2>&1; then
      echo "Disposable link remains: ${name}" >&2
      failed=1
    fi
  done
  if ip netns list | awk '{print $1}' | grep -Fxq "${namespace}"; then
    echo "Disposable namespace remains: ${namespace}" >&2
    failed=1
  fi
  if ovs-vsctl br-exists "${bridge}" >/dev/null 2>&1; then
    echo "Disposable OVS bridge remains: ${bridge}" >&2
    failed=1
  fi
  if [[ -e ${state_dir} ]]; then
    echo "Verifier state remains: ${state_dir}" >&2
    failed=1
  fi
  return "${failed}"
}

on_exit() {
  local status=$?
  trap - EXIT
  trap '' INT TERM HUP
  if (( cleanup_done == 0 )); then
    cleanup_done=1
    cleanup || status=1
  fi
  rotate_evidence "${evidence_count_limit}" || status=1
  exit "${status}"
}

snapshot() {
  local label=$1 destination
  destination="${evidence_dir}/${label}"
  install -d -m 0750 "${destination}"
  docker ps -a --no-trunc --format '{{.ID}} {{.Names}}' | sort >"${destination}/docker-containers.txt"
  docker image ls --no-trunc --digests --format '{{.Repository}} {{.Tag}} {{.Digest}} {{.ID}}' | sort >"${destination}/docker-images.txt"
  ovs-vsctl --data=bare --no-heading --columns=_uuid,name list Bridge | sort >"${destination}/ovs-bridges.txt"
  ip netns list | sort >"${destination}/namespaces.txt"
  ip -j link show | jq -S . >"${destination}/links.json"
  ip -j route show table all | jq -S . >"${destination}/routes.json"
  nft -j list ruleset | jq -S 'walk(if type == "object" and has("counter") then .counter.bytes = 0 | .counter.packets = 0 else . end)' >"${destination}/nftables.json"
  ps -eo pid=,comm=,args= | grep -E '(^|/)(dockerd|ovsdb-server|ovs-vswitchd|qemu-system-|tcpdump|dumpcap|tshark)( |$)' | sort >"${destination}/processes.txt" || true
  ss -Hlnptu | sort >"${destination}/listeners.txt"
}

compare_snapshot() {
  local file
  for file in docker-containers.txt docker-images.txt ovs-bridges.txt namespaces.txt links.json routes.json nftables.json processes.txt listeners.txt; do
    cmp -s "${evidence_dir}/before/${file}" "${evidence_dir}/after/${file}" || {
      diff -u "${evidence_dir}/before/${file}" "${evidence_dir}/after/${file}" >"${evidence_dir}/difference-${file}.txt" || true
      fail "unrelated guest state changed (${file})"
      return 1
    }
  done
}

critical_state_create() {
  (
    trap '' INT TERM HUP
    umask 077
    mkdir -- "${state_dir}" || exit 1
    if ! printf '%s\n' "${token}" >"${state_dir}/owner"; then
      rmdir -- "${state_dir}" 2>/dev/null || true
      exit 1
    fi
  )
}

critical_bridge_create() {
  (
    trap '' INT TERM HUP
    ovs-vsctl --timeout=10 add-br "${bridge}" -- set Bridge "${bridge}" \
      datapath_type=system external_ids:graphx_m1_owner="${token}" || exit 1
    current=$(ovs-vsctl get Bridge "${bridge}" _uuid | tr -d '"')
    if ! printf '%s\n' "${current}" >"${state_dir}/bridge.uuid"; then
      ovs-vsctl --timeout=10 --if-exists del-br "${bridge}" || true
      exit 1
    fi
  )
}

critical_namespace_create() {
  (
    trap '' INT TERM HUP
    ip netns add "${namespace}" || exit 1
    current=$(stat -Lc '%i' "/run/netns/${namespace}")
    if ! printf '%s\n' "${current}" >"${state_dir}/namespace.inode"; then
      ip netns delete "${namespace}" || true
      exit 1
    fi
  )
}

critical_internal_create() {
  (
    trap '' INT TERM HUP
    ovs-vsctl --timeout=10 add-port "${bridge}" "${internal}" -- set Interface "${internal}" \
      type=internal external_ids:graphx_m1_owner="${token}" || exit 1
    current=$(cat "/sys/class/net/${internal}/ifindex")
    if ! printf '%s\n' "${current}" >"${state_dir}/internal.ifindex"; then
      ovs-vsctl --timeout=10 --if-exists del-port "${bridge}" "${internal}" || true
      exit 1
    fi
  )
}

critical_veth_create() {
  (
    trap '' INT TERM HUP
    ip link add "${veth_host}" type veth peer name "${veth_ns}" || exit 1
    host_index=$(cat "/sys/class/net/${veth_host}/ifindex")
    peer_index=$(cat "/sys/class/net/${veth_ns}/ifindex")
    if ! printf '%s\n' "${host_index}" >"${state_dir}/veth-host.ifindex" || \
      ! printf '%s\n' "${peer_index}" >"${state_dir}/veth-ns.ifindex"; then
      ip link delete "${veth_host}" || true
      exit 1
    fi
  )
}

critical_tap_create() {
  (
    trap '' INT TERM HUP
    ip tuntap add dev "${tap}" mode tap user "${tap_user}" || exit 1
    current=$(cat "/sys/class/net/${tap}/ifindex")
    if ! printf '%s\n' "${current}" >"${state_dir}/tap.ifindex"; then
      ip link delete "${tap}" || true
      exit 1
    fi
  )
}

for command in docker ovs-vsctl ovs-ofctl ip tc nft tcpdump tshark qemu-system-aarch64 cmake ninja jq openssl; do
  command -v "${command}" >/dev/null || fail "required command is missing: ${command}"
done
[[ $(uname -m) == aarch64 ]] || fail "guest architecture is not aarch64"
[[ $(cat /etc/graphx-m1-config.sha256) == "${GRAPHX_M1_CONFIG_DIGEST:?missing expected digest}" ]] || fail "guest configuration identity does not match"
login_user=${GRAPHX_LIMA_USER:-${SUDO_USER:-}}
if [[ -z ${login_user} ]]; then
  login_user=$(stat -c %U /workspace/graphx-docker)
fi
[[ ${login_user} != root ]] || fail "Lima verification must be entered through the login user"
id -nG "${login_user}" | tr ' ' '\n' | grep -Fxq docker || fail "Lima login user lacks Docker access"
sudo -u "${login_user}" docker info >/dev/null || fail "Lima login user cannot access rootful Docker"
mountpoint -q /workspace/graphx-docker || fail "source checkout is not mounted"
source_type=$(findmnt -n -o FSTYPE -T /workspace/graphx-docker)
state_type=$(findmnt -n -o FSTYPE -T /var/lib/graphx)
[[ ${source_type} == virtiofs && ${state_type} != virtiofs ]] || fail "runtime state is not isolated from the shared virtiofs mount"
for path in /var/lib/docker /var/lib/openvswitch /var/lib/graphx/qemu /var/lib/graphx/captures; do
  [[ $(findmnt -n -o SOURCE -T "${path}") == "$(findmnt -n -o SOURCE -T /var/lib/graphx)" ]] || fail "${path} is not VM-local"
done
systemctl is-active --quiet docker.service || fail "rootful Docker system service is not active"
systemctl is-active --quiet openvswitch-switch.service || fail "Open vSwitch system service is not active"
[[ $(docker info --format '{{.DockerRootDir}}') == /var/lib/docker ]] || fail "Docker data root is unexpected"
install -d -m 0750 "${evidence_root}"
docker compose version >"${evidence_root}/compose-version.txt"
docker buildx version >"${evidence_root}/buildx-version.txt"
ovs-vsctl --timeout=10 show >/dev/null

[[ ! -e ${state_dir} ]] || fail "verifier state already exists: ${state_dir}"
for name in "${bridge}" "${namespace}" "${veth_host}" "${veth_ns}" "${tap}" "${internal}"; do
  ! ip link show dev "${name}" >/dev/null 2>&1 || fail "disposable name is occupied: ${name}"
done
! ip netns list | awk '{print $1}' | grep -Fxq "${namespace}" || fail "disposable namespace name is occupied"
! ovs-vsctl br-exists "${bridge}" >/dev/null 2>&1 || fail "disposable OVS bridge name is occupied"
! docker inspect "${container}" >/dev/null 2>&1 || fail "disposable Docker name is occupied"

rotate_evidence "$((evidence_count_limit - 1))"
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
critical_state_create
maybe_fail state
install -d -m 0750 "${evidence_dir}"
snapshot before

timeout 30 docker run --rm --name "${container}" --label "graphx.m1.owner=${token}" --network none hello-world:linux >"${evidence_dir}/docker-probe.txt"
maybe_fail docker

critical_bridge_create
maybe_fail bridge
critical_internal_create
ip link set dev "${internal}" alias "graphx-m1:${token}"
ip address add 10.254.83.1/30 dev "${internal}"
ip link set dev "${internal}" up
maybe_fail internal

critical_namespace_create
maybe_fail namespace-created
ip netns exec "${namespace}" ip link set dev lo alias "graphx-m1:${token}"
maybe_fail namespace-owned

critical_veth_create
maybe_fail veth-created
ip link set dev "${veth_host}" alias "graphx-m1:${token}"
ip link set dev "${veth_ns}" alias "graphx-m1:${token}"
maybe_fail veth-owned
ip link set dev "${veth_ns}" netns "${namespace}"
ip netns exec "${namespace}" ip link set lo up
ip netns exec "${namespace}" ip address add 10.254.83.2/30 dev "${veth_ns}"
ip netns exec "${namespace}" ip link set dev "${veth_ns}" up
ovs-vsctl --timeout=10 add-port "${bridge}" "${veth_host}" -- set Interface "${veth_host}" external_ids:graphx_m1_owner="${token}"
ip link set dev "${veth_host}" up
maybe_fail veth-attached

tap_user=${SUDO_USER:-$(stat -c '%U' /workspace/graphx-docker)}
critical_tap_create
maybe_fail tap-created
ip link set dev "${tap}" alias "graphx-m1:${token}"
maybe_fail tap-owned
ip link set dev "${tap}" up
ovs-vsctl --timeout=10 add-port "${bridge}" "${tap}" -- set Interface "${tap}" external_ids:graphx_m1_owner="${token}"
maybe_fail tap-attached

ip netns exec "${namespace}" tc qdisc add dev "${veth_ns}" root netem delay 1ms
maybe_fail netem
ip netns exec "${namespace}" nft add table inet "${nft_table}"
ip netns exec "${namespace}" nft 'add chain inet gx_m1 input { type filter hook input priority 0; policy accept; }'
ip netns exec "${namespace}" nft add rule inet "${nft_table}" input ip protocol icmp counter accept
maybe_fail topology

timeout 12 tcpdump -U -i "${internal}" -c 2 -w "${evidence_dir}/packet.pcap" icmp >"${evidence_dir}/tcpdump.stdout" 2>"${evidence_dir}/tcpdump.stderr" &
capture_pid=$!
maybe_fail capture-started
timeout 10 bash -c 'until grep -q "listening on" "$1"; do kill -0 "$2" || exit 1; sleep 0.1; done' _ "${evidence_dir}/tcpdump.stderr" "${capture_pid}"
ip netns exec "${namespace}" timeout 10 ping -c 2 -W 2 10.254.83.1 >"${evidence_dir}/ping.txt"
timeout 15 bash -c 'while kill -0 "$1" 2>/dev/null; do sleep 0.1; done' _ "${capture_pid}"
wait "${capture_pid}"
capture_pid=
[[ -s ${evidence_dir}/packet.pcap && $(stat -c %s "${evidence_dir}/packet.pcap") -le 1048576 ]] || fail "capture is absent or exceeds 1 MiB"
tshark -r "${evidence_dir}/packet.pcap" -c 2 >"${evidence_dir}/tshark.txt"

{
  printf 'bridge_datapath='; ovs-vsctl get Bridge "${bridge}" datapath_type
  ovs-vsctl --columns=_uuid,name,type,external_ids list Interface
  ip -d link show dev "${tap}"
  ip netns exec "${namespace}" ip -d link show dev "${veth_ns}"
  ip netns exec "${namespace}" ip route show
  ip netns exec "${namespace}" nft list ruleset
  ip netns exec "${namespace}" tc qdisc show dev "${veth_ns}"
  printf 'qemu_accelerators='; qemu-system-aarch64 -accel help | tr '\n' ' '
} >"${evidence_dir}/runtime-evidence.txt"
grep -Eq '^bridge_datapath="?system"?$' "${evidence_dir}/runtime-evidence.txt" || fail "OVS did not use the system datapath"

cd /workspace/graphx-docker
install -d -m 0755 /var/lib/graphx/m1/build
GRAPHX_DEV_BUILD_DIR=/var/lib/graphx/m1/build/dev \
  GRAPHX_VERIFY_LOG_DIR="${evidence_dir}/verification-log" \
  timeout 1800 scripts/verify.sh quick >"${evidence_dir}/graphx-quick.txt" 2>&1
timeout 60 /var/lib/graphx/m1/build/dev/graphx project graphx.yaml --check --output-dir config >"${evidence_dir}/graphx-projections.txt" 2>&1

cleanup_done=1
cleanup || fail "disposable topology cleanup was incomplete"
assert_clean || fail "disposable topology remains after cleanup"
snapshot after
compare_snapshot

{
  echo "status=passed"
  echo "timestamp=${timestamp}"
  echo "instance=graphx"
  echo "token=${token}"
  echo "guest_arch=$(uname -m)"
  echo "kernel=$(uname -sr)"
  echo "source_fstype=${source_type}"
  echo "state_fstype=${state_type}"
  echo "docker=$(docker version --format '{{.Server.Version}}')"
  echo "buildx=$(docker buildx version)"
  echo "ovs=$(ovs-vsctl --version | head -n 1)"
} >"${evidence_dir}/result.txt"

rotate_evidence "${evidence_count_limit}"
trap - EXIT INT TERM HUP
echo "GraphX M1 verification passed; evidence: ${evidence_dir}"

#!/usr/bin/env bash
set -euo pipefail

[[ $(uname -s) == Linux && ${EUID} -eq 0 ]] || {
  echo "provision.sh must run as root inside the Lima Linux guest." >&2
  exit 1
}
: "${GRAPHX_LIMA_USER:?Lima user identity was not provided}"
: "${GRAPHX_M1_CONFIG_DIGEST:?configuration digest was not provided}"

export DEBIAN_FRONTEND=noninteractive
packages=(
  build-essential ca-certificates clang cmake curl docker-buildx docker-compose-v2 docker.io
  gnupg iproute2 jq libssl-dev libyaml-cpp-dev nftables ninja-build nodejs npm
  openvswitch-switch openssl pkg-config python3 python3-jsonschema python3-yaml
  qemu-system-arm qemu-system-ppc qemu-system-x86 qemu-utils tcpdump tshark
)

apt_retry() {
  local attempt
  for attempt in 1 2 3; do
    if timeout 1200 apt-get "$@"; then
      return 0
    fi
    if [[ ${attempt} -lt 3 ]]; then
      sleep $((attempt * 5))
    fi
  done
  echo "apt-get $* failed after three bounded attempts." >&2
  return 1
}

apt_retry update
printf 'wireshark-common wireshark-common/install-setuid boolean false\n' | debconf-set-selections
apt_retry install --no-install-recommends -y "${packages[@]}"

install -d -m 0755 /etc/docker
cat >/etc/docker/daemon.json <<'EOF'
{
  "data-root": "/var/lib/docker",
  "iptables": true,
  "live-restore": false,
  "log-driver": "local",
  "log-opts": {"max-size": "10m", "max-file": "3"}
}
EOF

install -d -m 0755 /var/lib/graphx /var/lib/graphx/qemu
install -d -m 0700 /var/lib/graphx/runs
install -d -m 0750 /var/lib/graphx/captures /var/lib/graphx/m1 /var/lib/graphx/m1/evidence
install -d -m 0755 /var/log/graphx
chown -R "${GRAPHX_LIMA_USER}:${GRAPHX_LIMA_USER}" /var/lib/graphx /var/log/graphx
chown root:root /var/lib/graphx/captures
chmod 0750 /var/lib/graphx/captures

# M6 runs QEMU without root while GraphX retains privileged ownership of the
# TAP/OVS lifecycle. Keep this numeric identity aligned with tap_uid/tap_gid in
# the checked-in QEMU TAP profile.
if ! getent group graphx-qemu >/dev/null; then
  groupadd --system --gid 65532 graphx-qemu
fi
if ! getent passwd graphx-qemu >/dev/null; then
  useradd --system --uid 65532 --gid graphx-qemu --home-dir /var/lib/graphx/qemu \
    --shell /usr/sbin/nologin graphx-qemu
fi
chown graphx-qemu:graphx-qemu /var/lib/graphx/qemu
chmod 0750 /var/lib/graphx/qemu

systemctl enable docker.service docker.socket openvswitch-switch.service
systemctl restart docker.service openvswitch-switch.service
timeout 60 bash -c 'until systemctl is-active --quiet docker.service; do sleep 2; done'
timeout 60 bash -c 'until systemctl is-active --quiet openvswitch-switch.service; do sleep 2; done'
usermod --append --groups docker "${GRAPHX_LIMA_USER}"
timeout 60 runuser --user "${GRAPHX_LIMA_USER}" -- docker info >/dev/null
timeout 180 docker pull hello-world:linux

printf '%s\n' "${GRAPHX_M1_CONFIG_DIGEST}" >/etc/graphx-m1-config.sha256
dpkg-query -W -f='${binary:Package}\t${Version}\n' "${packages[@]}" \
  | sort >/var/lib/graphx/m1/package-versions.tsv
{
  printf 'provisioned_at='; date -u +%Y-%m-%dT%H:%M:%SZ
  printf 'kernel='; uname -srvm
  printf 'architecture='; dpkg --print-architecture
  printf 'config_digest=%s\n' "${GRAPHX_M1_CONFIG_DIGEST}"
  printf 'docker_probe_image='; docker image inspect hello-world:linux --format '{{index .RepoDigests 0}}'
  printf 'docker_buildx='; docker buildx version
} >/var/lib/graphx/m1/provisioned
chown -R "${GRAPHX_LIMA_USER}:${GRAPHX_LIMA_USER}" /var/lib/graphx/m1

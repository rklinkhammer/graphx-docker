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
  build-essential ca-certificates clang cmake curl docker-compose-v2 docker.io
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

install -d -m 0755 /var/lib/graphx /var/lib/graphx/runs /var/lib/graphx/qemu
install -d -m 0750 /var/lib/graphx/captures /var/lib/graphx/m1 /var/lib/graphx/m1/evidence
install -d -m 0755 /var/log/graphx
chown -R "${GRAPHX_LIMA_USER}:${GRAPHX_LIMA_USER}" /var/lib/graphx /var/log/graphx

systemctl enable docker.service docker.socket openvswitch-switch.service
systemctl restart docker.service openvswitch-switch.service
timeout 60 bash -c 'until systemctl is-active --quiet docker.service; do sleep 2; done'
timeout 60 bash -c 'until systemctl is-active --quiet openvswitch-switch.service; do sleep 2; done'
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
} >/var/lib/graphx/m1/provisioned
chown -R "${GRAPHX_LIMA_USER}:${GRAPHX_LIMA_USER}" /var/lib/graphx/m1

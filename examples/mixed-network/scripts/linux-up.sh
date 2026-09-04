#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
[[ "$(uname -s)" == Linux ]] || { echo "native profile requires Linux" >&2; exit 2; }

sudo -v

# Refuse to adopt or overwrite an existing lab. This also catches resources left
# by an interrupted earlier create and gives the operator a deterministic
# recovery command before any new host state is changed.
existing=()
for interface in mv-ovs mv-parent cap-mac-ovs cap-mac iv-ovs iv-parent cap-ipv-ovs cap-ipv \
                 r-mac ovs-r-mac r-ipv ovs-r-ipv; do
  if sudo ip link show dev "$interface" >/dev/null 2>&1; then
    existing+=("interface:$interface")
  fi
done
for bridge in br-gx-mac br-gx-ipv; do
  if sudo ovs-vsctl br-exists "$bridge" >/dev/null 2>&1; then
    existing+=("bridge:$bridge")
  fi
done
if sudo ip netns list | awk '$1 == "gx-router" { found=1 } END { exit !found }'; then
  existing+=("namespace:gx-router")
fi
for network in gx-mac-domain gx-ipv-domain; do
  if docker network inspect "$network" >/dev/null 2>&1; then
    existing+=("network:$network")
  fi
done

if ((${#existing[@]})); then
  echo "GraphX mixed-network infrastructure already exists or is partially present:" >&2
  printf '  - %s\n' "${existing[@]}" >&2
  echo >&2
  echo "Clean it up, then retry:" >&2
  echo "  examples/mixed-network/scripts/linux-down.sh" >&2
  exit 2
fi

rollback() {
  local status=$?
  if ((status != 0)); then
    echo "Mixed-network startup failed; rolling back resources created by this attempt." >&2
    docker compose -p gx-mac-side -f "$example_dir/compose/macvlan.compose.yml" \
      down --remove-orphans >/dev/null 2>&1 || true
    docker compose -p gx-ipv-side -f "$example_dir/compose/ipvlan.compose.yml" \
      down --remove-orphans >/dev/null 2>&1 || true
    sudo "$graphx" infra destroy "$example_dir/graphx.yaml" || true
  fi
  exit "$status"
}
trap rollback EXIT

sudo "$graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-mac-side -f "$example_dir/compose/macvlan.compose.yml" up -d --build
docker compose -p gx-ipv-side -f "$example_dir/compose/ipvlan.compose.yml" up -d --build
trap - EXIT
echo "Mixed-network lab is running."
echo "Status: examples/mixed-network/scripts/status.sh"
echo "Stop:   examples/mixed-network/scripts/linux-down.sh"

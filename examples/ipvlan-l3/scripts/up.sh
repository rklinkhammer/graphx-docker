#!/usr/bin/env bash
set -euo pipefail
example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}/graphx"
[[ "$(uname -s)" == Linux ]] || { echo "ipvlan L3 demo requires a Linux Docker host" >&2; exit 2; }
sudo -v

if sudo ip link show gx-l3-parent >/dev/null 2>&1 ||
   docker network inspect gx-ipvl3-domains >/dev/null 2>&1; then
  echo "IPvlan L3 infrastructure already exists or a previous run was interrupted." >&2
  printf 'Run GRAPHX_BUILD_DIR=%q examples/ipvlan-l3/scripts/down.sh, then retry.\n' \
    "$(dirname "$graphx")" >&2
  exit 2
fi

rollback() {
  local status=$?
  if ((status != 0)); then
    echo "IPvlan L3 startup failed; rolling back resources created by this attempt." >&2
    "$example_dir/scripts/down.sh" >/dev/null 2>&1 || true
  fi
  exit "$status"
}
trap rollback EXIT

sudo ip link add gx-l3-parent type dummy
sudo ip address replace 192.0.2.1/30 dev gx-l3-parent
sudo ip link set gx-l3-parent up
sudo "$graphx" infra create "$example_dir/graphx.yaml"
docker compose -p gx-ipvl3-generator -f "$example_dir/compose/generator.compose.yml" up -d --build
docker compose -p gx-ipvl3-transform -f "$example_dir/compose/transform.compose.yml" up -d
docker compose -p gx-ipvl3-sink -f "$example_dir/compose/sink.compose.yml" up -d

printf 'Waiting for end-to-end samples'
for _ in {1..60}; do
  if docker logs gx-ipvl3-sink-sink-1 2>&1 | grep 'sink seq=' >/dev/null; then
    printf ' ready\n'
    break
  fi
  printf '.'
  sleep 1
done
if ! docker logs gx-ipvl3-sink-sink-1 2>&1 | grep 'sink seq=' >/dev/null; then
  printf ' timed out\n' >&2
  echo "No sample reached the sink within 60 seconds." >&2
  exit 1
fi
trap - EXIT
echo "IPvlan L3 pipeline is running."
echo "Follow sink: docker logs -f gx-ipvl3-sink-sink-1"

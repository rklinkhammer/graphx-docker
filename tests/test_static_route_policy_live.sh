#!/usr/bin/env bash
set -euo pipefail

test "$#" -eq 2 || { echo "usage: $0 GRAPHX_CLI SOURCE_ROOT" >&2; exit 64; }
graphx=$1
root=$2
demo="$root/examples/static-route-policy/scripts/demo.sh"

GRAPHX_BIN="$graphx" "$demo" status
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.2.10
! sudo ip netns exec gx-route-middle-end ping -c 1 -W 1 10.64.1.10
! sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
sudo ip netns exec gx-route-router nft list chain inet graphx forward
GRAPHX_BIN="$graphx" "$demo" apply-route
sudo ip netns exec gx-route-router ip route show 10.64.30.10/32
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
GRAPHX_BIN="$graphx" "$demo" clear-route
GRAPHX_BIN="$graphx" "$demo" status

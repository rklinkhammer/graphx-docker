#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
[[ $(uname -s) == Linux && ${GRAPHX_ALLOW_PRIVILEGED_TESTS:-0} == 1 ]] || {
  echo "Requires Linux and explicit GRAPHX_ALLOW_PRIVILEGED_TESTS=1" >&2; exit 2;
}
: "${GRAPHX_IMAGE_RELEASE:?Set the verified image release directory}"
: "${GRAPHX_TEST_RELEASE:?Set the verified native/platform installation}"
: "${GRAPHX_GUEST_RELEASE:?Set the verified guest artifact directory}"
target=${GRAPHX_TEST_TARGET:-native-linux}
output=${GRAPHX_PRIVILEGED_EVIDENCE:-/var/lib/graphx/runtime/acceptance-$(date -u +%Y%m%dT%H%M%SZ)-$$}
[[ $output == /var/lib/graphx/* && ! -e $output ]] || {
  echo "Evidence must be an absent directory under /var/lib/graphx" >&2; exit 2;
}
docker info >/dev/null
docker compose version
runner=(sudo -n)
[[ $EUID != 0 ]] || runner=()
for example in macvlan ipvlan-l2 ipvlan-l3 mixed-network network-observability static-route-policy; do
  "${runner[@]}" python3 "$ROOT/tests/test_ovs_execution_live.py" --allow-privileged \
    --target "$target" --images "$GRAPHX_IMAGE_RELEASE" --release "$GRAPHX_TEST_RELEASE" \
    --case "$example" --output "$output/$example"
done
for scenario in S11 S12 S14; do
  "${runner[@]}" python3 "$ROOT/tests/test_scenario_live.py" --allow-privileged \
    --target "$target" --images "$GRAPHX_IMAGE_RELEASE" --release "$GRAPHX_TEST_RELEASE" \
    --case "$scenario" --output "$output/$scenario"
done
for guest in S15 T03; do
  extra=()
  [[ $guest != S15 ]] || extra=(--scenario)
  "${runner[@]}" python3 "$ROOT/tests/test_guest_execution_live.py" --allow-privileged \
    --target "$target" --images "$GRAPHX_IMAGE_RELEASE" --release "$GRAPHX_TEST_RELEASE" \
    --guests "$GRAPHX_GUEST_RELEASE" --case "$guest" "${extra[@]}" --output "$output/$guest"
done
printf 'Privileged compiled acceptance passed: %s\n' "$output"

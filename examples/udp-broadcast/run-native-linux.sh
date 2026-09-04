#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
build_dir="${GRAPHX_BUILD_DIR:-$repo_dir/build/dev}"
publisher="$build_dir/graphx-udp-publisher"
subscriber="$build_dir/graphx-udp-subscriber"
publisher_ns=gx-udp-publisher
listener_ns=gx-udp-listener
bridge=gxudp-br
log_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-udp-broadcast-native.XXXXXX")"
capture_pid=

cleanup() {
  if [[ -n "$capture_pid" ]]; then
    kill "$capture_pid" 2>/dev/null || true
    wait "$capture_pid" 2>/dev/null || true
  fi
  "$example_dir/down-native-linux.sh" >/dev/null 2>&1 || true
  find "$log_dir" -type f -delete 2>/dev/null || true
  rmdir "$log_dir" 2>/dev/null || true
}
interrupted() {
  trap - EXIT
  cleanup
  exit 130
}
trap cleanup EXIT
trap interrupted INT TERM

[[ "$(uname -s)" == Linux ]] || {
  echo "Native UDP broadcast verification requires a Linux host" >&2
  exit 2
}
command -v ip >/dev/null || { echo "Missing required command: ip" >&2; exit 2; }
command -v sudo >/dev/null || { echo "Missing required command: sudo" >&2; exit 2; }
case "${GRAPHX_VERIFY_LIVE_CAPTURE:-0}" in
  0) ;;
  1)
    command -v dumpcap >/dev/null || { echo "Live capture requires dumpcap" >&2; exit 2; }
    command -v tshark >/dev/null || { echo "Live capture requires tshark" >&2; exit 2; }
    command -v timeout >/dev/null || { echo "Live capture requires timeout" >&2; exit 2; }
    ;;
  *) echo "GRAPHX_VERIFY_LIVE_CAPTURE must be 0 or 1" >&2; exit 2 ;;
esac
[[ -x "$publisher" && -x "$subscriber" ]] || {
  echo "Build graphx-udp-publisher and graphx-udp-subscriber first" >&2
  exit 2
}

if sudo ip netns list | grep -Eq "^($publisher_ns|$listener_ns)( |$)" ||
   ip link show "$bridge" >/dev/null 2>&1; then
  echo "Native UDP broadcast resources already exist; run down-native-linux.sh first" >&2
  exit 2
fi

sudo ip netns add "$listener_ns"
sudo ip netns add "$publisher_ns"
sudo ip link add "$bridge" type bridge
sudo ip link set "$bridge" up

sudo ip link add gxudp-lh type veth peer name gxudp-ln
sudo ip link set gxudp-ln netns "$listener_ns"
sudo ip link set gxudp-lh master "$bridge"
sudo ip link set gxudp-lh up
sudo ip -n "$listener_ns" link set gxudp-ln name eth0
sudo ip -n "$listener_ns" address add 172.31.91.2/24 dev eth0
sudo ip -n "$listener_ns" link set lo up
sudo ip -n "$listener_ns" link set eth0 up

sudo ip link add gxudp-ph type veth peer name gxudp-pn
sudo ip link set gxudp-pn netns "$publisher_ns"
sudo ip link set gxudp-ph master "$bridge"
sudo ip link set gxudp-ph up
sudo ip -n "$publisher_ns" link set gxudp-pn name eth0
sudo ip -n "$publisher_ns" address add 172.31.91.3/24 dev eth0
sudo ip -n "$publisher_ns" link set lo up
sudo ip -n "$publisher_ns" link set eth0 up

if [[ "${GRAPHX_VERIFY_LIVE_CAPTURE:-0}" == 1 ]]; then
  capture_file="$log_dir/udp-live.pcapng"
  install -m 0600 /dev/null "$capture_file"
  # Redirection intentionally belongs to the invoking user in its private temp directory.
  # shellcheck disable=SC2024
  sudo timeout 10 dumpcap -q -i gxudp-ph -f 'udp dst port 47102' \
    -c 5 -a duration:8 -w "$capture_file" >"$log_dir/dumpcap.log" 2>&1 &
  capture_pid=$!
  # Give dumpcap a bounded opportunity to attach before the five datagrams.
  sleep 0.5
fi

# Redirection intentionally belongs to the invoking user in its private temp directory.
# shellcheck disable=SC2024
sudo ip netns exec "$listener_ns" env \
  GRAPHX_CONFIG="$example_dir/graphx.yaml" GRAPHX_MAX_MESSAGES=5 \
  "$subscriber" >"$log_dir/listener.log" 2>&1 &
listener_pid=$!
sleep 0.2
sudo ip netns exec "$publisher_ns" env \
  GRAPHX_CONFIG="$example_dir/graphx.yaml" GRAPHX_MAX_MESSAGES=5 \
  GRAPHX_MESSAGE_PREFIX=discovery GRAPHX_START_DELAY_MS=100 \
  "$publisher"
wait "$listener_pid"
cat "$log_dir/listener.log"
grep -q '^PASS received=5$' "$log_dir/listener.log"

if [[ "${GRAPHX_VERIFY_LIVE_CAPTURE:-0}" == 1 ]]; then
  wait "$capture_pid"
  capture_pid=
  sudo chmod 0644 "$capture_file"
  install -m 0644 "$repo_dir/wireshark/graphx.lua" "$log_dir/graphx.lua"
  tshark -n -X "lua_script:$log_dir/graphx.lua" -r "$capture_file" \
    -Y 'udp.dstport == 47102 && graphx.version == 2' \
    -T fields -e graphx.sequence >"$log_dir/sequences.txt"
  diff -u <(printf '1\n2\n3\n4\n5\n') "$log_dir/sequences.txt"
  echo "PASS live-capture sequences=1-5"
fi

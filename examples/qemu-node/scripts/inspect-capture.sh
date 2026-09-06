#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 QEMU_RUN_DIRECTORY [TSHARK_DISPLAY_FILTER]" >&2
}

test "$#" -ge 1 && test "$#" -le 2 || { usage; exit 64; }

run_dir=$1
display_filter=${2:-'tcp.port == 18001 || udp.port == 18001 || tcp.port == 19001 || udp.port == 19001'}
capture=$run_dir/qemu-node.pcapng
tshark_bin=${TSHARK:-tshark}

command -v "$tshark_bin" >/dev/null || {
  echo "missing prerequisite: $tshark_bin" >&2
  exit 2
}
test -f "$capture" || {
  echo "QEMU PCAPNG does not exist: $capture" >&2
  exit 2
}
test -r "$capture" || {
  echo "QEMU PCAPNG is not readable by uid=$(id -u) gid=$(id -g): $capture" >&2
  ls -ld "$run_dir" "$capture" >&2 || true
  exit 2
}

# Some confined Linux TShark packages cannot open files below a project
# workspace even when the invoking operator can read them. Open the retained
# artifact in this shell and stream it over stdin so TShark never opens the
# workspace path itself.
exec "$tshark_bin" -r - -Y "$display_filter" <"$capture"

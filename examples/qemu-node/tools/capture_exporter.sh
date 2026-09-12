#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

if test "$#" -ne 6; then
  echo "usage: $0 PID_FILE GRAPHX CONFIG CAPTURE_ID HANDOFF_DIRECTORY GID" >&2
  exit 64
fi

pid_file=$1
graphx=$2
config=$3
capture_id=$4
handoff_dir=$5
consumer_gid=$6
destination=$handoff_dir/qemu-span.pcapng
temporary=$handoff_dir/.qemu-span.$$.pcapng

cleanup() {
  rm -f "$temporary" "$pid_file"
}
trap cleanup EXIT
trap 'exit 0' INT TERM
printf '%s\n' "$$" >"$pid_file"

while :; do
  rm -f "$temporary"
  if GRAPHX_OVERRIDES= "$graphx" infra capture export "$config" \
      --capture "$capture_id" --output "$temporary" >/dev/null 2>&1; then
    chown "root:$consumer_gid" "$temporary"
    chmod 0440 "$temporary"
    mv -f "$temporary" "$destination"
  fi
  sleep 0.25
done

#!/usr/bin/env bash
set -euo pipefail

telemetry_first="graphx-phase5-telemetry-first-$$"
native_first="graphx-phase5-native-first-$$"

cleanup() {
  docker volume rm "$telemetry_first" "$native_first" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

docker volume create "$telemetry_first" >/dev/null
docker volume create "$native_first" >/dev/null

docker run --rm --entrypoint /bin/sh -v "$telemetry_first:/captures" \
  graphx-telemetry:latest -c \
  'test "$(stat -c %u:%g:%a /captures)" = 65532:65532:770 && test -r /captures'
docker run --rm --entrypoint /bin/sh -v "$telemetry_first:/captures" \
  graphx-demo:latest -c 'touch /captures/native-after-telemetry'

docker run --rm --entrypoint /bin/sh -v "$native_first:/captures" \
  graphx-demo:latest -c \
  'test "$(stat -c %u:%g:%a /captures)" = 65532:65532:770 && touch /captures/native-first.pcapng'
docker run --rm --entrypoint /bin/sh -v "$native_first:/captures:ro" \
  graphx-telemetry:latest -c \
  'test -r /captures/native-first.pcapng && ! touch /captures/collector-write 2>/dev/null'

echo 'capture volume ownership and read-only collector checks passed in both initialization orders'


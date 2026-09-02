#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PROJECT=${GRAPHX_PHASE7_TEST_PROJECT:-"graphx-phase7-test-$$"}
OBSERVATION_TOKEN=${GRAPHX_PHASE7_OBSERVATION_TOKEN:-"phase7-observation-token-0123456789abcdef"}
FILES=(-f "$ROOT/compose.yaml" -f "$ROOT/compose.history.yaml")

compose() {
  GRAPHX_OBSERVATION_TOKEN="$OBSERVATION_TOKEN" docker compose -p "$PROJECT" "${FILES[@]}" "$@"
}

cleanup() {
  compose down --timeout 6 -v >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

for tool in docker curl node; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

compose config --quiet
compose up -d --build telemetry
telemetry="$PROJECT-telemetry-1"

wait_for_history() {
  local ready=false
  local deadline=$((SECONDS + 60))
  while ((SECONDS < deadline)); do
    if [[ $(docker inspect -f '{{.State.Health.Status}}' "$telemetry" 2>/dev/null || true) == healthy ]] &&
       curl -fsS -H "Authorization: Bearer $OBSERVATION_TOKEN" \
         'http://127.0.0.1:8080/api/history/status' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try { if (JSON.parse(input).status !== "ready") process.exit(1) } catch { process.exit(1) }
})'; then
      ready=true
      break
    fi
    sleep 0.25
  done
  if [[ "$ready" != true ]]; then
    compose ps -a >&2 || true
    docker logs --tail 100 "$telemetry" >&2 || true
    return 1
  fi
}

history_contains_sequence() {
  curl -fsS -H "Authorization: Bearer $OBSERVATION_TOKEN" \
    'http://127.0.0.1:8080/api/history?limit=100&node=generator' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    if (!JSON.parse(input).records.some(record => record.data.sequence === 7007)) process.exit(1)
  } catch { process.exit(1) }
})'
}

wait_for_history
unauthorized_status=$(curl -sS -o /dev/null -w '%{http_code}' \
  'http://127.0.0.1:8080/api/history/status')
[[ "$unauthorized_status" == 401 ]] || {
  echo "history status without an observation credential returned $unauthorized_status, expected 401" >&2
  exit 1
}
docker exec "$telemetry" node -e '
const socket = require("node:dgram").createSocket("udp4")
const event = Buffer.from(JSON.stringify({kind:"trace",event:"send",nodeId:"generator",edgeId:"samples",sequence:7007,timestamp:Date.now(),wireBytes:64}))
socket.send(event, 9000, "127.0.0.1", error => { socket.close(); if (error) process.exitCode=1 })'

deadline=$((SECONDS + 15))
until history_contains_sequence; do
  ((SECONDS < deadline)) || { echo "durable history record did not appear" >&2; exit 1; }
  sleep 0.1
done

compose restart telemetry >/dev/null
wait_for_history
history_contains_sequence

echo "Phase 7 durable history write, authenticated API path, restart persistence, and Compose volume checks passed"

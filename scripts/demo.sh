#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
COMPOSE=(docker compose -f "$ROOT/compose.yaml")
URL=${GRAPHX_DEMO_URL:-http://127.0.0.1:8080}
OBSERVATION_HEADERS=()
if test -n "${GRAPHX_OBSERVATION_TOKEN:-}"; then
  OBSERVATION_HEADERS=(-H "Authorization: Bearer $GRAPHX_OBSERVATION_TOKEN")
fi

usage() {
  cat <<'EOF'
Usage: scripts/demo.sh <start|verify|status|logs|stop>

  start   Build and start the complete Docker demo, then verify data flow
  verify  Prove that services, TCP edges, samples, and telemetry are live
  status  Show container status and the latest sink output
  logs    Follow generator, transform, sink, and telemetry output
  stop    Stop the demo and remove its containers and private bridge network
EOF
}

require() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing prerequisite: $1" >&2
    exit 2
  }
}

wait_for_telemetry() {
  printf 'Waiting for the telemetry service'
  for _ in {1..60}; do
    if curl -fsS "$URL/api/health" >/dev/null 2>&1; then
      printf ' ready\n'
      return
    fi
    printf '.'
    sleep 1
  done
  printf ' timed out\n' >&2
  "${COMPOSE[@]}" ps >&2
  return 1
}

received_count() {
  local edge=$1
  awk -v edge="$edge" '
    $1 ~ /^graphx_edge_messages_total/ &&
    $1 ~ ("edge=\"" edge "\"") &&
    $1 ~ /direction="received"/ { print $2 }
  '
}

connected_value() {
  local edge=$1
  awk -v edge="$edge" '
    $1 ~ /^graphx_edge_connected/ && $1 ~ ("edge=\"" edge "\"") { print $2 }
  '
}

verify() {
  require docker
  require curl
  wait_for_telemetry

  local running metrics_before metrics_after first_samples first_transformed
  local second_samples second_transformed edge
  running=$("${COMPOSE[@]}" ps --status running --services)
  for edge in telemetry sink transform generator; do
    grep -qx "$edge" <<<"$running" || {
      echo "FAIL: service '$edge' is not running" >&2
      "${COMPOSE[@]}" ps >&2
      return 1
    }
  done

  metrics_before=$(curl -fsS "${OBSERVATION_HEADERS[@]}" "$URL/metrics")
  first_samples=$(received_count samples <<<"$metrics_before")
  first_transformed=$(received_count transformed <<<"$metrics_before")
  sleep 2
  metrics_after=$(curl -fsS "${OBSERVATION_HEADERS[@]}" "$URL/metrics")
  second_samples=$(received_count samples <<<"$metrics_after")
  second_transformed=$(received_count transformed <<<"$metrics_after")

  for edge in samples transformed; do
    test "$(connected_value "$edge" <<<"$metrics_after")" = 1 || {
      echo "FAIL: TCP edge '$edge' is not connected" >&2
      return 1
    }
  done
  test -n "$first_samples" && test -n "$first_transformed" || {
    echo "FAIL: telemetry counters are missing" >&2
    return 1
  }
  test "$second_samples" -gt "$first_samples" || {
    echo "FAIL: samples did not move from generator to transform" >&2
    return 1
  }
  test "$second_transformed" -gt "$first_transformed" || {
    echo "FAIL: transformed samples did not move to sink" >&2
    return 1
  }

  echo "PASS: all four services are running"
  echo "PASS: both TCP edges are connected"
  echo "PASS: samples edge advanced $first_samples -> $second_samples"
  echo "PASS: transformed edge advanced $first_transformed -> $second_transformed"
  echo "PASS: telemetry API and Prometheus metrics are live"
  echo
  echo "Console: $URL"
  echo "Follow output: scripts/demo.sh logs"
  echo "Stop cleanly: scripts/demo.sh stop"
}

case ${1:-} in
  start)
    require docker
    require curl
    "${COMPOSE[@]}" up -d --build
    verify
    ;;
  verify)
    verify
    ;;
  status)
    require docker
    "${COMPOSE[@]}" ps
    echo
    echo "Latest values received by the sink:"
    "${COMPOSE[@]}" logs --tail=12 sink | grep 'sink seq=' | tail -n 5 || true
    ;;
  logs)
    require docker
    "${COMPOSE[@]}" logs -f --tail=30 generator transform sink telemetry
    ;;
  stop)
    require docker
    "${COMPOSE[@]}" down --remove-orphans
    ;;
  *)
    usage
    exit 64
    ;;
esac

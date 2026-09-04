#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
COMPOSE=(docker compose -f "$ROOT/compose.yaml" -f "$ROOT/compose.history.yaml")
URL=${GRAPHX_DEMO_URL:-http://127.0.0.1:8080}
DEMO_STATE_DIR=${GRAPHX_DEMO_STATE_DIR:-"$ROOT/.graphx"}
DEMO_ENV_FILE="$DEMO_STATE_DIR/demo.env"

usage() {
  cat <<'EOF'
Usage: scripts/demo.sh <start|verify|status|logs|token|stop> [options]

  start   Start with bounded capture/history and generated local credentials
  verify  Prove that services, TCP edges, samples, and telemetry are live
  status  Show container status and the latest sink output
  logs    Follow generator, transform, sink, and telemetry output
  token   Print the browser control token
  stop    Stop the demo and remove its containers and private bridge network

Start options:
  --no-capture  Disable application PCAPNG capture
  --no-history  Disable durable SQLite telemetry history
EOF
}

require() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing prerequisite: $1" >&2
    exit 2
  }
}

valid_demo_credential() {
  local value=$1
  test "${#value}" -ge 32 && test "${#value}" -le 4096 || return 1
  case "$value" in *[![:graph:]]*) return 1 ;; esac
}

ensure_demo_credentials() {
  local stored_control stored_runtime temporary
  if ! test -f "$DEMO_ENV_FILE"; then
    require openssl
    mkdir -p "$DEMO_STATE_DIR"
    chmod 0700 "$DEMO_STATE_DIR"
    temporary="$DEMO_ENV_FILE.tmp.$$"
    umask 077
    printf 'GRAPHX_CONTROL_TOKEN=%s\nGRAPHX_TELEMETRY_SHARED_SECRET=%s\n' \
      "$(openssl rand -hex 32)" "$(openssl rand -hex 32)" >"$temporary"
    chmod 0600 "$temporary"
    mv "$temporary" "$DEMO_ENV_FILE"
  fi
  chmod 0600 "$DEMO_ENV_FILE"
  stored_control=$(sed -n 's/^GRAPHX_CONTROL_TOKEN=//p' "$DEMO_ENV_FILE")
  stored_runtime=$(sed -n 's/^GRAPHX_TELEMETRY_SHARED_SECRET=//p' "$DEMO_ENV_FILE")
  if ! valid_demo_credential "$stored_control" ||
      ! valid_demo_credential "$stored_runtime" ||
      test "$stored_control" = "$stored_runtime"; then
    echo "Invalid demo credential file: $DEMO_ENV_FILE" >&2
    echo "Move it aside and rerun this command to generate a replacement." >&2
    return 1
  fi
  if test -z "${GRAPHX_CONTROL_TOKEN:-}"; then
    export GRAPHX_CONTROL_TOKEN="$stored_control"
  fi
  if test -z "${GRAPHX_TELEMETRY_SHARED_SECRET:-}"; then
    export GRAPHX_TELEMETRY_SHARED_SECRET="$stored_runtime"
  fi
  if test "$GRAPHX_CONTROL_TOKEN" = "$GRAPHX_TELEMETRY_SHARED_SECRET"; then
    echo "The demo control token and runtime secret must be distinct." >&2
    return 1
  fi
  if ! valid_demo_credential "$GRAPHX_CONTROL_TOKEN" ||
      ! valid_demo_credential "$GRAPHX_TELEMETRY_SHARED_SECRET"; then
    echo "Demo credentials must contain 32-4096 printable non-space characters." >&2
    return 1
  fi
  if test "$GRAPHX_CONTROL_TOKEN" != "$stored_control" ||
      test "$GRAPHX_TELEMETRY_SHARED_SECRET" != "$stored_runtime"; then
    temporary="$DEMO_ENV_FILE.tmp.$$"
    umask 077
    printf 'GRAPHX_CONTROL_TOKEN=%s\nGRAPHX_TELEMETRY_SHARED_SECRET=%s\n' \
      "$GRAPHX_CONTROL_TOKEN" "$GRAPHX_TELEMETRY_SHARED_SECRET" >"$temporary"
    chmod 0600 "$temporary"
    mv "$temporary" "$DEMO_ENV_FILE"
  fi
}

configure_demo_features() {
  export GRAPHX_CAPTURE_ENABLED=${GRAPHX_CAPTURE_ENABLED:-true}
  export GRAPHX_CAPTURE_MAX_FILE_BYTES=${GRAPHX_CAPTURE_MAX_FILE_BYTES:-67108864}
  export GRAPHX_CAPTURE_MAX_PACKETS=${GRAPHX_CAPTURE_MAX_PACKETS:-100000}
  export GRAPHX_HISTORY_ENABLED=${GRAPHX_HISTORY_ENABLED:-true}
  export GRAPHX_HISTORY_RETENTION_SECONDS=${GRAPHX_HISTORY_RETENTION_SECONDS:-86400}
  export GRAPHX_HISTORY_MAX_RECORDS=${GRAPHX_HISTORY_MAX_RECORDS:-50000}
  export GRAPHX_HISTORY_MAX_DATABASE_BYTES=${GRAPHX_HISTORY_MAX_DATABASE_BYTES:-67108864}
}

observed_get() {
  if test -n "${GRAPHX_OBSERVATION_TOKEN:-}"; then
    curl -fsS -H "Authorization: Bearer $GRAPHX_OBSERVATION_TOKEN" "$1"
  else
    curl -fsS "$1"
  fi
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

  metrics_before=$(observed_get "$URL/metrics")
  first_samples=$(received_count samples <<<"$metrics_before")
  first_transformed=$(received_count transformed <<<"$metrics_before")
  sleep 2
  metrics_after=$(observed_get "$URL/metrics")
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

demo_command=${1:-}
if test -n "$demo_command"; then shift; fi
disable_capture=false
disable_history=false
while test "$#" -gt 0; do
  case "$1" in
    --no-capture) disable_capture=true ;;
    --no-history) disable_history=true ;;
    *) usage; exit 64 ;;
  esac
  shift
done
if test "$demo_command" != start &&
    { test "$disable_capture" = true || test "$disable_history" = true; }; then
  usage
  exit 64
fi

case "$demo_command" in
  start)
    require docker
    require curl
    ensure_demo_credentials
    configure_demo_features
    if test "$disable_capture" = true; then export GRAPHX_CAPTURE_ENABLED=false; fi
    if test "$disable_history" = true; then export GRAPHX_HISTORY_ENABLED=false; fi
    "${COMPOSE[@]}" up -d --build
    verify
    echo
    echo "Capture: $GRAPHX_CAPTURE_ENABLED · History: $GRAPHX_HISTORY_ENABLED"
    echo "Control token (paste into the console): $GRAPHX_CONTROL_TOKEN"
    echo "Local credentials: $DEMO_ENV_FILE (mode 0600)"
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
  token)
    ensure_demo_credentials
    printf '%s\n' "$GRAPHX_CONTROL_TOKEN"
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

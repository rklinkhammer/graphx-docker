#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
source "$ROOT/scripts/test-feature-common.sh"
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-compose-test.XXXXXX")
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT INT TERM
docker_suite() {
  local docker_http_port=${GRAPHX_DOCKER_TEST_HTTP_PORT:-28080}
  require docker
  docker compose version >/dev/null || {
    echo "Docker acceptance requires Docker Compose" >&2
    return 2
  }
  docker info >/dev/null || {
    echo "Docker acceptance requires a reachable engine for the selected context" >&2
    return 2
  }
  "$ROOT/scripts/test-features.sh" portable
  step "Validate and smoke-test the standard Compose deployment"
  export GRAPHX_PUBLISHED_HTTP_PORT=$docker_http_port
  docker compose -f "$ROOT/compose.yaml" config >/dev/null
  docker compose -f "$ROOT/compose.yaml" up --build --force-recreate normalize-config
  docker compose -f "$ROOT/compose.yaml" up -d --build
  trap 'docker compose -f "$ROOT/compose.yaml" down --remove-orphans; cleanup' EXIT INT TERM
  for _ in {1..60}; do
    curl -fsS "http://127.0.0.1:$docker_http_port/api/health" >/dev/null && break
    sleep 1
  done
  "$ROOT/scripts/demo.sh" verify
  docker compose -f "$ROOT/compose.yaml" ps
  docker compose -f "$ROOT/compose.yaml" down --remove-orphans
  step "Run isolated UDP broadcast example"
  "$ROOT/examples/udp-broadcast/run.sh"
  trap cleanup EXIT INT TERM
  step "Docker feature suite passed"
}


docker_suite


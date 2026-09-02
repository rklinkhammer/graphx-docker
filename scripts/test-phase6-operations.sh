#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PROJECT=${GRAPHX_PHASE6_TEST_PROJECT:-"graphx-phase6-test-$$"}
FILES=(-f "$ROOT/compose.yaml" -f "$ROOT/compose.observability.yaml")

cleanup() {
  docker compose -p "$PROJECT" "${FILES[@]}" down --timeout 6 -v >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

for tool in docker curl node; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

docker compose -p "$PROJECT" "${FILES[@]}" config --quiet
docker compose -p "$PROJECT" "${FILES[@]}" up -d --build telemetry prometheus grafana

telemetry="$PROJECT-telemetry-1"
prometheus="$PROJECT-prometheus-1"
grafana="$PROJECT-grafana-1"
grafana_password=${GRAPHX_GRAFANA_ADMIN_PASSWORD:-graphx-local-only}

target_is_up() {
  curl -fsS 'http://127.0.0.1:9090/api/v1/targets?state=active' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    const targets = JSON.parse(input).data.activeTargets
    if (!targets.some(target => target.health === "up" && target.labels.job === "graphx")) process.exit(1)
  } catch { process.exit(1) }
})'
}

readiness_sample_exists() {
  curl -fsS 'http://127.0.0.1:9090/api/v1/query?query=graphx_service_ready' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    const result = JSON.parse(input).data.result
    if (!result.some(sample => sample.value?.[1] === "1")) process.exit(1)
  } catch { process.exit(1) }
})'
}

rules_are_loaded() {
  curl -fsS 'http://127.0.0.1:9090/api/v1/rules?type=alert' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    const groups = JSON.parse(input).data.groups
    if (groups.reduce((count, group) => count + group.rules.length, 0) !== 6) process.exit(1)
  } catch { process.exit(1) }
})'
}

dashboard_is_loaded() {
  curl -fsS -u "admin:$grafana_password" \
    'http://127.0.0.1:3000/api/search?query=GraphX%20Operations' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    const dashboards = JSON.parse(input)
    if (!dashboards.some(value => value.uid === "graphx-operations")) process.exit(1)
  } catch { process.exit(1) }
})'
}

diagnostics() {
  echo "Phase 6 operations stack did not satisfy its acceptance checks before the deadline" >&2
  docker compose -p "$PROJECT" "${FILES[@]}" ps -a >&2 || true
  curl -fsS 'http://127.0.0.1:9090/api/v1/targets?state=active' 2>/dev/null | node -e '
let input=""; process.stdin.on("data", value => input += value).on("end", () => {
  try {
    for (const target of JSON.parse(input).data.activeTargets)
      console.error(JSON.stringify({ health: target.health, lastError: target.lastError, labels: target.labels }))
  } catch { console.error("Prometheus target diagnostics unavailable") }
})' || true
  for container in "$telemetry" "$prometheus" "$grafana"; do
    echo "Last 80 log lines for $container" >&2
    docker logs --tail 80 "$container" >&2 || true
  done
}

ready=false
deadline=$((SECONDS + 60))
while ((SECONDS < deadline)); do
  telemetry_health=$(docker inspect -f '{{.State.Health.Status}}' "$telemetry" 2>/dev/null || true)
  prometheus_health=$(docker inspect -f '{{.State.Health.Status}}' "$prometheus" 2>/dev/null || true)
  if [[ "$telemetry_health" == healthy && "$prometheus_health" == healthy ]] &&
     curl -fsS http://127.0.0.1:3000/api/health >/dev/null 2>&1 &&
     target_is_up && readiness_sample_exists && rules_are_loaded && dashboard_is_loaded; then
    ready=true
    break
  fi
  sleep 0.5
done

if [[ "$ready" != true ]]; then
  diagnostics
  exit 1
fi

echo "Phase 6 telemetry, Prometheus, rules, scrape, query, and Grafana checks passed"

#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/configure-build-trust.sh"
PROJECT=${GRAPHX_PHASE6_OTLP_PROJECT:-"graphx-phase6-otlp-$$"}
PORT=${GRAPHX_PHASE6_OTLP_PORT:-18438}
TEMP=$(mktemp -d "${TMPDIR:-/tmp}/graphx-phase6-otlp.XXXXXX")
FILES=(-f "$ROOT/compose.yaml" -f "$ROOT/compose.otlp-secure.yaml" -f "$ROOT/compose.otlp-mtls.yaml")
receiver_pid=

cleanup() {
  docker compose -p "$PROJECT" "${FILES[@]}" down --timeout 6 -v >/dev/null 2>&1 || true
  if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" >/dev/null 2>&1 || true; wait "$receiver_pid" 2>/dev/null || true; fi
  rm -rf "$TEMP"
}
trap cleanup EXIT INT TERM

for tool in docker node openssl; do
  command -v "$tool" >/dev/null || { echo "missing prerequisite: $tool" >&2; exit 2; }
done

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj '/CN=GraphX Phase 6 Test CA' \
  -keyout "$TEMP/ca.key" -out "$TEMP/ca.pem" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -subj '/CN=host.docker.internal' \
  -addext 'subjectAltName=DNS:host.docker.internal' -addext 'extendedKeyUsage=serverAuth' \
  -keyout "$TEMP/server.key" -out "$TEMP/server.csr" >/dev/null 2>&1
openssl x509 -req -days 1 -in "$TEMP/server.csr" -CA "$TEMP/ca.pem" -CAkey "$TEMP/ca.key" \
  -CAcreateserial -copy_extensions copy -out "$TEMP/server.pem" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -subj '/CN=graphx-compose-client' \
  -addext 'extendedKeyUsage=clientAuth' -keyout "$TEMP/client.key" \
  -out "$TEMP/client.csr" >/dev/null 2>&1
openssl x509 -req -days 1 -in "$TEMP/client.csr" -CA "$TEMP/ca.pem" -CAkey "$TEMP/ca.key" \
  -CAcreateserial -copy_extensions copy -out "$TEMP/client.pem" >/dev/null 2>&1
openssl rand -hex 32 > "$TEMP/token"

GRAPHX_TEST_TOKEN_FILE="$TEMP/token" GRAPHX_TEST_SERVER_KEY="$TEMP/server.key" \
GRAPHX_TEST_SERVER_CERT="$TEMP/server.pem" GRAPHX_TEST_CA_FILE="$TEMP/ca.pem" \
GRAPHX_TEST_RESULT_FILE="$TEMP/result" GRAPHX_TEST_PORT="$PORT" \
  node "$ROOT/scripts/otlp-mtls-receiver.mjs" > "$TEMP/receiver.log" 2>&1 &
receiver_pid=$!
for _ in $(seq 1 40); do grep -q '^ready$' "$TEMP/receiver.log" 2>/dev/null && break; sleep 0.1; done
grep -q '^ready$' "$TEMP/receiver.log"

export GRAPHX_OTLP_ENDPOINT="https://host.docker.internal:$PORT"
export GRAPHX_OTLP_AUTH_TOKEN_FILE="$TEMP/token"
export GRAPHX_OTLP_CA_FILE="$TEMP/ca.pem"
export GRAPHX_OTLP_CERT_FILE="$TEMP/client.pem"
export GRAPHX_OTLP_KEY_FILE="$TEMP/client.key"
export GRAPHX_OTLP_EXPORT_INTERVAL_MS=250
export GRAPHX_OTLP_RETRY_MAX_ATTEMPTS=1
docker compose -p "$PROJECT" "${FILES[@]}" config --quiet
docker compose -p "$PROJECT" "${FILES[@]}" up -d telemetry

for _ in $(seq 1 80); do [[ -s "$TEMP/result" ]] && break; sleep 0.25; done
[[ -s "$TEMP/result" ]]
grep -q 'authorized OTLP metric export' "$TEMP/result"
echo "Phase 6 secure Compose token, private-CA, and mTLS export passed"

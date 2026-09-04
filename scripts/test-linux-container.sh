#!/usr/bin/env bash
set -Eeuo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
MODE=${1:-tls}
IMAGE=${GRAPHX_LINUX_VERIFIER_IMAGE:-graphx-linux-verifier:local}
EVIDENCE_DIR=${GRAPHX_LINUX_EVIDENCE_DIR:-"$ROOT/outputs/linux-container"}

case "$MODE" in
  tls|ctest|portable|quality|sanitizers|fuzz|shell) ;;
  *)
    echo "usage: $0 {tls|ctest|portable|quality|sanitizers|fuzz|shell}" >&2
    exit 64
    ;;
esac

command -v docker >/dev/null || {
  echo "missing prerequisite: docker" >&2
  exit 2
}
docker info >/dev/null

mkdir -p "$EVIDENCE_DIR"

build=(docker build --progress=plain -f "$ROOT/docker/linux-verifier.Dockerfile" -t "$IMAGE")
if test -n "${GRAPHX_CA_CERT:-}"; then
  test -r "$GRAPHX_CA_CERT" || {
    echo "GRAPHX_CA_CERT is not readable: $GRAPHX_CA_CERT" >&2
    exit 2
  }
  build+=(--secret "id=graphx_ca,src=$GRAPHX_CA_CERT")
fi
if test -n "${GRAPHX_CERT_INSTALL_SCRIPT:-}"; then
  test -r "$GRAPHX_CERT_INSTALL_SCRIPT" || {
    echo "GRAPHX_CERT_INSTALL_SCRIPT is not readable: $GRAPHX_CERT_INSTALL_SCRIPT" >&2
    exit 2
  }
  build+=(--secret "id=graphx_cert_installer,src=$GRAPHX_CERT_INSTALL_SCRIPT")
fi
build+=("$ROOT")

echo "Building Linux verifier image: $IMAGE"
"${build[@]}"

echo "Running Linux verifier mode: $MODE"
echo "Evidence directory: $EVIDENCE_DIR"
run=(docker run --rm --init \
  --name "graphx-linux-${MODE}-$$" \
  --shm-size 512m \
  --mount "type=bind,src=$EVIDENCE_DIR,dst=/evidence")
if test -n "${GRAPHX_FUZZ_SECONDS:-}"; then
  run+=(--env "GRAPHX_FUZZ_SECONDS=$GRAPHX_FUZZ_SECONDS")
fi
run+=("$IMAGE" "$MODE")
"${run[@]}"

#!/usr/bin/env bash
set -euo pipefail

SMOKE=${1:?usage: test-tls.sh /path/to/graphx-tls-smoke}
TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-tls-test.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=GraphX test CA' -keyout "$TMP_DIR/ca.key" -out "$TMP_DIR/ca.pem" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -sha256 -subj '/CN=localhost' \
  -keyout "$TMP_DIR/peer.key" -out "$TMP_DIR/peer.csr" >/dev/null 2>&1
printf '%s\n' 'subjectAltName=DNS:localhost,IP:127.0.0.1' 'extendedKeyUsage=serverAuth,clientAuth' \
  >"$TMP_DIR/extensions.cnf"
openssl x509 -req -in "$TMP_DIR/peer.csr" -CA "$TMP_DIR/ca.pem" -CAkey "$TMP_DIR/ca.key" \
  -CAcreateserial -days 1 -sha256 -extfile "$TMP_DIR/extensions.cnf" -out "$TMP_DIR/peer.pem" \
  >/dev/null 2>&1
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=GraphX untrusted CA' -keyout "$TMP_DIR/untrusted-ca.key" \
  -out "$TMP_DIR/untrusted-ca.pem" >/dev/null 2>&1

port=$((20000 + ($$ % 20000)))
"$SMOKE" "$TMP_DIR/peer.pem" "$TMP_DIR/peer.key" "$TMP_DIR/ca.pem" "$port" \
  "$TMP_DIR/untrusted-ca.pem"

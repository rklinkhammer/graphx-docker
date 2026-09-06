#!/usr/bin/env bash
set -euo pipefail
umask 077

output=${1:?usage: generate_tls.sh OUTPUT_DIRECTORY}
mkdir -p "$output"
temporary=$(mktemp -d "$output/.tls.XXXXXX")
cleanup() { rm -rf "$temporary"; }
trap cleanup EXIT

openssl req -x509 -newkey rsa:2048 -sha256 -nodes -days 7 \
  -subj /CN=GraphX-SDR-demo-CA \
  -addext basicConstraints=critical,CA:TRUE \
  -addext keyUsage=critical,keyCertSign,cRLSign \
  -keyout "$temporary/ca.key" -out "$temporary/ca.pem" >/dev/null 2>&1
for identity in sdr-node processor; do
  openssl req -newkey rsa:2048 -sha256 -nodes -subj "/CN=$identity" \
    -keyout "$temporary/$identity.key" -out "$temporary/$identity.csr" >/dev/null 2>&1
  if test "$identity" = sdr-node; then
    printf 'subjectAltName=DNS:sdr-node,IP:172.30.13.10,IP:10.63.0.10\nextendedKeyUsage=serverAuth\n' >"$temporary/extensions"
  else
    printf 'extendedKeyUsage=clientAuth\n' >"$temporary/extensions"
  fi
  openssl x509 -req -sha256 -days 7 -in "$temporary/$identity.csr" \
    -CA "$temporary/ca.pem" -CAkey "$temporary/ca.key" -CAcreateserial \
    -extfile "$temporary/extensions" -out "$temporary/$identity.pem" >/dev/null 2>&1
done
install -m 0600 "$temporary/ca.pem" "$temporary/sdr-node.pem" "$temporary/sdr-node.key" \
  "$temporary/processor.pem" "$temporary/processor.key" "$output/"

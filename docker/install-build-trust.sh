#!/usr/bin/env bash
set -Eeuo pipefail

ca_secret=/run/secrets/graphx_ca
installer_secret=/run/secrets/graphx_cert_installer
trust_changed=false
trust_fingerprint=${GRAPHX_BUILD_TRUST_FINGERPRINT:-graphx-trust-v1-none}

if { test -s "$ca_secret" || test -s "$installer_secret"; } &&
   test "$trust_fingerprint" = graphx-trust-v1-none; then
  echo "build trust was supplied without a content fingerprint; use a repository entry-point script or source scripts/configure-build-trust.sh" >&2
  exit 2
fi

if ! test -s "$ca_secret" && ! test -s "$installer_secret" &&
   test "$trust_fingerprint" != graphx-trust-v1-none; then
  echo "build trust fingerprint was supplied without its BuildKit secrets" >&2
  exit 2
fi

if test -s "$ca_secret"; then
  install -m 0644 "$ca_secret" \
    /usr/local/share/ca-certificates/graphx-organization-root-ca.crt
  trust_changed=true
fi

if test -s "$installer_secret"; then
  /usr/bin/bash "$installer_secret"
  trust_changed=true
fi

if test "$trust_changed" = true; then
  update-ca-certificates
fi

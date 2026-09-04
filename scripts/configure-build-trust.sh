#!/usr/bin/env bash

graphx_sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
  else
    openssl dgst -sha256 "$1" | awk '{print $NF}'
  fi
}

graphx_build_trust_material=""
if test -n "${GRAPHX_CA_CERT:-}"; then
  case "$GRAPHX_CA_CERT" in
    /*) ;;
    *) echo "GRAPHX_CA_CERT must be an absolute path" >&2; return 2 2>/dev/null || exit 2 ;;
  esac
  test -f "$GRAPHX_CA_CERT" && test -r "$GRAPHX_CA_CERT" || {
    echo "GRAPHX_CA_CERT is not a readable regular file: $GRAPHX_CA_CERT" >&2
    return 2 2>/dev/null || exit 2
  }
  graphx_build_trust_material="GRAPHX_CA_CERT=$(graphx_sha256_file "$GRAPHX_CA_CERT");"
fi

if test -n "${GRAPHX_CERT_INSTALL_SCRIPT:-}"; then
  case "$GRAPHX_CERT_INSTALL_SCRIPT" in
    /*) ;;
    *) echo "GRAPHX_CERT_INSTALL_SCRIPT must be an absolute path" >&2; return 2 2>/dev/null || exit 2 ;;
  esac
  test -f "$GRAPHX_CERT_INSTALL_SCRIPT" && test -r "$GRAPHX_CERT_INSTALL_SCRIPT" || {
    echo "GRAPHX_CERT_INSTALL_SCRIPT is not a readable regular file: $GRAPHX_CERT_INSTALL_SCRIPT" >&2
    return 2 2>/dev/null || exit 2
  }
  graphx_build_trust_material+="GRAPHX_CERT_INSTALL_SCRIPT=$(graphx_sha256_file "$GRAPHX_CERT_INSTALL_SCRIPT");"
fi

if test -n "$graphx_build_trust_material"; then
  if command -v sha256sum >/dev/null 2>&1; then
    GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-$(printf '%s' "$graphx_build_trust_material" | sha256sum | awk '{print $1}')
  elif command -v shasum >/dev/null 2>&1; then
    GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-$(printf '%s' "$graphx_build_trust_material" | shasum -a 256 | awk '{print $1}')
  else
    GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-$(printf '%s' "$graphx_build_trust_material" | openssl dgst -sha256 | awk '{print $NF}')
  fi
else
  GRAPHX_BUILD_TRUST_FINGERPRINT=graphx-trust-v1-none
fi
export GRAPHX_BUILD_TRUST_FINGERPRINT
unset graphx_build_trust_material

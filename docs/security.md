# GraphX security boundary

Phase 5 adds authentication, transport encryption, input validation, and
least-privilege container defaults. It does not turn telemetry controls into a
multi-user authorization service; role policy, identity-provider integration,
credential rotation workflows, and auditable decisions remain Phase 8.

## Threat model and defaults

GraphX treats graph configuration, framed TCP input, UDP telemetry, HTTP request
metadata, WebSocket upgrades, and capture names as untrusted. Secrets are never
stored in `graphx.yaml`. They enter processes through an environment variable or
a file-mounted counterpart ending in `_FILE`. The two forms are mutually
exclusive, values are limited to 4,096 bytes, and configured bearer/HMAC secrets
must contain at least 32 bytes. GraphX does not print secret values.

The telemetry service binds HTTP and UDP to `127.0.0.1` by default. Plain HTTP
cannot bind a non-loopback address unless
`GRAPHX_ALLOW_INSECURE_REMOTE=true` explicitly acknowledges that boundary. The
standard Compose demo publishes only `127.0.0.1:8080`, although its service binds
inside the private container network and therefore carries that explicit demo
override.

## TCP TLS 1.3 and mutual TLS

Every TCP edge accepts an additive `tls` object. Existing version-1 files without
it retain plaintext behavior. A secure edge uses TLS 1.3 or newer, disables TLS
compression, validates the certificate chain and peer DNS/IP name by default,
and may require a client certificate:

```yaml
transport:
  tcp:
    samples:
      host: transform.internal
      bind: 0.0.0.0
      port: 7001
      tls:
        enabled: true
        verify_peer: true
        require_client_certificate: true
        ca_file: /run/secrets/graphx-ca.pem
        certificate_file: /run/secrets/graphx-peer.pem
        private_key_file: /run/secrets/graphx-peer.key
        server_name: transform.internal
```

`certificate_file` and `private_key_file` are required together when TLS is
enabled. A CA file is required for mutual TLS; otherwise the system trust store
may be used. `server_name` overrides the transport host for certificate
verification and SNI. Errors identify the edge and endpoint without revealing
key material.

The integration test creates an ephemeral CA and dual-purpose peer certificate,
proves an encrypted mutual-TLS round trip, and requires a peer-name mismatch to
fail. Test credentials are deleted afterward.

## Telemetry datagram authentication

Set the same `GRAPHX_TELEMETRY_SHARED_SECRET` (or `_FILE`) on every runtime node
and the collector. A node wraps its original event as:

```json
{"payload":{"kind":"trace"},"auth":{"timestamp":0,"nonce":"32 lowercase hex characters","signature":"64 lowercase hex characters"}}
```

The signature is HMAC-SHA256 over
`timestamp + "." + nonce + "." + JSON.stringify(payload)`. The collector checks
the signature in constant time, allows at most 30 seconds of clock skew, and
rejects nonce replay with a bounded 4,096-entry cache. It then validates event
kind, node and edge identity, file names, identities, and numeric ranges before
mutating state or remembering a control endpoint. Datagrams larger than 16 KiB
are ignored.

Pause/resume commands and acknowledgements use the same envelope. Runtime
control cannot be enabled unless the collector has both a control bearer token
and the HMAC secret. Without an HMAC secret, legacy unsigned telemetry remains
available for loopback-only educational use, but HTTP runtime control is
disabled.

## HTTP, HTTPS, WebSocket, and API rules

Configure HTTPS with `GRAPHX_TLS_CERT_FILE` and `GRAPHX_TLS_KEY_FILE`. Both are
required together and TLS 1.3 is the minimum. Setting
`GRAPHX_TLS_CLIENT_CA_FILE` enables HTTPS mutual TLS.

| Surface | Method | Credential |
|---|---|---|
| `/api/health` | GET | public liveness only |
| `/api/topology`, `/api/captures`, `/api/history`, `/api/history/status`, `/metrics`, capture download | GET | `GRAPHX_OBSERVATION_TOKEN` when configured |
| configured WebSocket path | upgrade | observation token in `graphx-auth.<base64url>` subprotocol when configured |
| `/api/control/reset`, `/pause`, `/resume` | POST with no body | `GRAPHX_CONTROL_TOKEN`; same-origin/allowlisted Origin |

Bearer values use `Authorization: Bearer ...` and constant-time comparison.
Browser observation credentials are kept in session storage, not persistent
local storage. Configure additional browser origins as a comma-separated exact
list in `GRAPHX_ALLOWED_ORIGINS`; otherwise state-changing and WebSocket requests
must be same-origin.

The service applies a 16 KiB header limit, 2,048-character request-target limit,
64-header limit, 10-second request/header timeouts, 5-second keep-alive timeout,
100 requests per socket, 4 KiB WebSocket payload limit, 120 requests/minute/IP,
and 10 control requests/minute/IP. General and control rate state are isolated,
expire after their windows, and each has a hard 2,048-client cap with oldest-entry
eviction. Malformed HTTP targets receive 400 and malformed WebSocket upgrades are
closed without terminating the service. Explicit 400, 401, 403, 405, 409, 413, 414, 429,
and 503 outcomes preserve failure meaning. Security, no-store, frame-denial,
content-type, and same-origin resource headers apply to API responses. Static
and capture paths are resolved beneath fixed roots rather than trusted from the
Host header.

## Container policy

Standard Compose services run as the image's non-root user with all Linux
capabilities dropped, `no-new-privileges`, a read-only root filesystem, a bounded
PID count, an init process, and a temporary `/tmp`. Only the native nodes can
write the named capture volume; telemetry mounts it read-only. Both images seed
the volume as capture GID 65532 with mode `0770`, and the Node user is a member of
that group, so initialization order does not change node write or collector read
access. The native image includes only the OpenSSL runtime required by TLS.

Privileged OVS/macvlan/ipvlan laboratories have separate threat and capability
requirements and are not represented as hardened by these portable defaults.

## Operational limitations

- Shared bearer and HMAC credentials have no built-in rotation or revocation;
  restart processes after atomically replacing mounted secret files.
- Rate limiting is in-process and per collector instance, not a distributed
  denial-of-service control.
- HMAC replay checks require clocks within 30 seconds.
- TLS protects TCP graph edges only when each edge explicitly enables it.
- OTLP hardening, readiness, SLOs, and dashboards are Phase 6. Phase 7 durable
  metadata history reuses the observation boundary; protect its volume and
  backups as sensitive operational data. Fine-grained control authorization and
  audit remain Phase 8.

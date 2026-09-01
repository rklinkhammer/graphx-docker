# ADR 0006: Layered authentication and TLS boundaries

## Status

Accepted for Phase 5.

## Context

GraphX had plaintext TCP graph edges, unsigned UDP telemetry, permissive HTTP
and WebSocket access, and a bearer check covering only part of the control API.
An unsigned event could claim a configured node identity and become its remembered
control endpoint. Portable containers already ran as non-root, but retained
ambient capabilities, a writable root, and a world-writable capture directory.

Security must improve without embedding one orchestrator, identity provider,
certificate authority, or secret manager in the GraphX core. Existing
educational plaintext configuration also needs an explicit compatible path.

## Decision

1. TCP TLS is optional additive per-edge configuration. OpenSSL 3 supplies a TLS
   1.3 minimum, chain and host verification, SNI, and optional mutual TLS.
2. Runtime telemetry/control use a bounded HMAC-SHA256 envelope with timestamps,
   random nonces, constant-time verification, and replay rejection. Control is
   disabled unless both HMAC and bearer credentials are configured.
3. Observation and control use separate bearer credentials. Secret-file and
   inline forms are mutually exclusive; ambiguous configuration fails closed.
4. Telemetry defaults to loopback plaintext. HTTPS supports TLS 1.3 and optional
   client certificates. Origin, method, body, path, size, timeout, rate,
   WebSocket, and static-root checks are enforced at the boundary.
5. Portable Compose services drop capabilities, forbid privilege escalation,
   use read-only roots and bounded PIDs, and expose telemetry on host loopback.
   Privileged network laboratories remain separate.

## Consequences

- Existing version-1 graph files remain valid and plaintext unless TLS is
  explicitly enabled. Local read-only telemetry can run without credentials,
  but mutation controls cannot.
- OpenSSL 3 is a native build/runtime dependency installed in CI and the native
  container image.
- HMAC interoperability depends on the documented JSON payload serialization
  and a synchronized 30-second clock window.
- Bearer authentication is not the fine-grained, audited authorization plane
  planned for Phase 8.
- Read-only roots require future features to declare writable mounts.

## Rejected alternatives

- **Put credentials in `graphx.yaml`:** topology is commonly checked into source
  control and projected into several environments.
- **Use bearer authentication for UDP:** connectionless UDP needs message
  integrity and replay protection, not only a shared string field.
- **Make TLS mandatory immediately:** that would silently break existing local
  examples and deployments.
- **Implement OAuth/OIDC and policy roles now:** identity-provider and
  authorization policy design belongs to the later control-plane phase.

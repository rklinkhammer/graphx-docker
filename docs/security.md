# Security model

GraphX is intended for controlled laboratories. Its privileged network operations
must run only on a trusted native Linux host or the dedicated Lima guest.

- Configuration, normalized JSON, policy, credentials, and ownership ledgers are
  bounded regular files; symlinks and insecure permissions are rejected where the
  file carries authority.
- Infrastructure commands use argument arrays, not a shell. Resource names,
  addresses, routes, VLANs, and paths are validated before mutation.
- Every owned resource is labeled and recorded with stable identity. Cleanup fails
  closed if identity has changed.
- Control endpoints require bearer authentication, policy authorization, runtime
  identity checks, same-origin or allowlisted origins, idempotency keys, request
  bounds, and audit logging.
- TLS supports peer verification and mutual authentication. Private keys and bearer
  tokens are file-projected and redacted.
- Telemetry, history, captures, queues, requests, and responses have explicit size,
  count, and time limits.
- Docker, OVS, and QMP sockets are not forwarded from the Lima guest to macOS.

Report vulnerabilities using the private contact in [`../SECURITY.md`](../SECURITY.md).

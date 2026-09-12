# Current project decisions

This page records the active design constraints for GraphX 1.1.0.

- `graphx.yaml` with `version: 2` is the only configuration format.
- The C++ loader is authoritative; normalized JSON is the only configuration input
  accepted by telemetry.
- The [instance identity contract](instance-identity.md) distinguishes logical
  graph/node identity, deployment instances, execution freshness, and infrastructure
  ownership. The configuration model supports explicit instance selection and
  typed SDR source settings; current runtimes do not yet provide concurrent-instance
  guarantees.
- System Open vSwitch is the only managed data-plane backend.
- Docker Compose owns processes and management connectivity only.
- Containers use owned veth pairs; QEMU uses an owned TAP. All cleanup verifies
  stable identity and fails closed.
- macOS development uses OrbStack for portable workloads and the dedicated Lima
  guest for privileged Linux networking and QEMU laboratories.
- Envelope wire format 2 is the only accepted wire format. It carries canonical
  message, trace, and optional parent identities.
- Transports return typed receive outcomes and enforce bounded messages, queues,
  timeouts, and shutdown.
- Telemetry history uses the current SQLite schema; capture uses bounded PCAPNG.
- Control requests require authenticated policy, runtime identity, authorization,
  origin checks, bounded input, idempotency, and audit records.
- Release artifacts are reproducible, checksum-verified, and include an SPDX SBOM.
- Tests cover current behavior. Historical snapshots and superseded fixtures or
  superseded implementation evidence are not maintained in the worktree.

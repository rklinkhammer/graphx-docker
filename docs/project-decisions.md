# Current project decisions

This page records the active design constraints for GraphX 1.1.0.

- `graphx.yml` with `version: 3` is the only authored configuration format; normalized JSON uses contract version 2.
- P1 implements validation and normalization; P2 implements resolved node bindings and the local-ready/release application interface. P3 compiles deterministic artifacts. P4 packages shared images and derives release catalog pins from verified OCI bytes; the source catalog remains an unverified development input. P5 adds compiled platform consumption, explicit credential staging, bounded owned history, optional observability services and a pinned native Node/web companion. P6 adds finite native/portable execution with verified releases and identity-checked cleanup. OVS, guest and scenario execution remain gated with `E_PHASE_UNAVAILABLE`; see `design/graph-generation/implementation-plan.md`.
- The C++ loader is authoritative; normalized JSON is the only configuration input
  accepted by telemetry.
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

- Native and container applications cannot share one authored graph. Use separate
  graphs; each has its own explicit management and telemetry path.

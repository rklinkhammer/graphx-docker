# GraphX architecture decision records

This index is the canonical register of accepted GraphX architecture decisions.
Decision records describe durable constraints and rationale; the current system
view is maintained in [`../GraphX_Architecture.md`](../GraphX_Architecture.md).

| ADR | Decision | Status | Date | Record |
|---:|---|---|---|---|
| 0001 | Authoritative versioned GraphX configuration | Accepted | 2026-08-28 | [`0001-authoritative-configuration.md`](0001-authoritative-configuration.md) |
| 0002 | Network infrastructure is a peer layer | Accepted | 2026-08-29 | [`0002-network-infrastructure-layer.md`](0002-network-infrastructure-layer.md) |
| 0003 | Typed receive outcomes and bounded local queues | Accepted | 2026-08-31 | [`0003-typed-receive-and-bounded-runtime.md`](0003-typed-receive-and-bounded-runtime.md) |
| 0004 | Version 2 envelopes and explicit identities | Accepted | 2026-08-31 | [`0004-envelope-v2-identities-and-compatibility.md`](0004-envelope-v2-identities-and-compatibility.md) |
| 0005 | Portable quality gates and bounded fuzzing | Accepted for Phase 4 | 2026-09-06 | [`0005-quality-gates-and-fuzzing.md`](0005-quality-gates-and-fuzzing.md) |
| 0006 | Layered authentication and TLS boundaries | Accepted for Phase 5 | 2026-09-01 | [`0006-phase-5-security-boundaries.md`](0006-phase-5-security-boundaries.md) |
| 0007 | Centralized bounded OTLP export and operational health | Accepted | 2026-09-01 | [`0007-centralized-bounded-otlp-and-operational-health.md`](0007-centralized-bounded-otlp-and-operational-health.md) |
| 0008 | Isolated SQLite telemetry history | Accepted | 2026-09-03 | [`0008-isolated-sqlite-telemetry-history.md`](0008-isolated-sqlite-telemetry-history.md) |
| 0009 | Authorized runtime control plane | Accepted | 2026-09-03 | [`0009-authorized-runtime-control-plane.md`](0009-authorized-runtime-control-plane.md) |
| 0010 | Bounded PCAPNG with a Lua dissector and validating extcap | Accepted | 2026-09-03 | [`0010-bounded-pcapng-wireshark-extcap.md`](0010-bounded-pcapng-wireshark-extcap.md) |
| 0011 | Immutable release artifacts and explicit compatibility | Accepted | 2026-09-03 | [`0011-immutable-release-artifacts-and-explicit-compatibility.md`](0011-immutable-release-artifacts-and-explicit-compatibility.md) |
| 0012 | Bounded IPv4 UDP edges | Accepted for Phase 11 | 2026-09-04 | [`0012-bounded-ipv4-udp-edges.md`](0012-bounded-ipv4-udp-edges.md) |
| 0013 | Unified external and containerized QEMU profiles | Accepted for Phase 12 | 2026-09-05 | [`0013-unified-qemu-profiles.md`](0013-unified-qemu-profiles.md) |
| 0014 | External device attachment and control relationships | Accepted for Phase 13 | 2026-09-06 | [`0014-external-device-control-cycles.md`](0014-external-device-control-cycles.md) |
| 0015 | Explicit manual route activation | Accepted for Phase 14 implementation | 2026-09-06 | [`0015-explicit-manual-route-activation.md`](0015-explicit-manual-route-activation.md) |

## Historical number mapping

The unified QEMU profiles decision was initially recorded as ADR 0012 at
`0012-unified-qemu-profiles.md`. It is canonicalized as ADR 0013 because ADR
0012 was already assigned to bounded IPv4 UDP edges. The old filename is
intentionally not retained as a second decision or symlink.

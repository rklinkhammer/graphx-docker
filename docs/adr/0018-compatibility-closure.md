# ADR 0018: Version 2 is the sole active network realization

- Status: Accepted for migration M8
- Date: 2026-09-10

## Context

M2 introduced an explicit version-2 semantic model, M3-M7 implemented and
verified one identity-owned OVS/veth/TAP lifecycle, and M5 retained the older
Docker macvlan/ipvlan and Docker Desktop simulation paths only for a bounded
compatibility period. Keeping those mutation paths callable after equivalence
testing would preserve two control planes and ambiguous operational guidance.

## Decision

Configuration version 2 is the only active GraphX-managed network realization.
Version-1 files remain accepted by validation, inspection, projection, and the
deterministic migration command during the published compatibility window, but
all infrastructure actions reject them before mutation and direct the operator
to `graphx config migrate`.

The production Docker-network planner and imperative legacy netem planner are
removed. Canonical network laboratories use version-2 `graphx.yaml`, OVS system
datapaths, veth or TAP endpoints, and management-only Compose connectivity. The
privileged Docker Desktop userspace-OVS simulation is retired. On macOS, Lima is
the Linux execution boundary.

QEMU TAP/OVS is the default GraphX QEMU path. The external and container slirp
profiles remain explicit, deprecated compatibility paths until their separate
published window ends; no default launcher selects them.

## Consequences

- A legacy configuration can be reviewed and migrated but cannot create,
  inspect, alter, or delete infrastructure.
- There is one network ownership and cleanup model in production code.
- Docker Compose remains useful for process lifetime and management traffic but
  does not own the GraphX data plane.
- Old native-driver behavior remains reproducible from version-control history,
  not from an active privileged launcher.
- Release claims still require truthful platform evidence; Lima/TCG cannot
  substitute for native x86_64 KVM verification.

# Current project decisions

This page records the active design constraints for GraphX 1.1.0.

- `graphx.yml` with `version: 3` is the only authored configuration format; normalized JSON uses contract version 2.
- The compiled execution path provides generic node bindings, deterministic artifacts,
  verified shared releases, default platform/history, credential staging, owned native
  and container processes, system OVS networking, verified QEMU TCG guests and explicit
  scenario actions. Examples have one authored source and no source Compose overlays.
  See `design/graph-generation/p10-verification.md` for current acceptance and remaining
  release qualification gates.
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

- Physical-device startup remains gated until an explicit physical-uplink ownership
  contract is designed. An external address alone does not authorize attaching a
  host NIC or silently starting the laboratory simulator. S14 is not a P7 live case.

- Laboratory SDR execution is an explicit `compile --laboratory ACTION_ID` selection
  from the original graph, with isolated owned networking and declared test credentials.
  It never substitutes for an already running physical device or mutates physical trust.
- S15 scenario verification follows the resolved P8 unicast TCP/UDP contract. Guest
  broadcast echo and multicast reception require a separate application contract.

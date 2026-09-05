# GraphX implementation work package

You are the implementation agent for the GraphX project located at:

`~/workspace/graphx-docker`

## Assignment

Implement **Phase 12: unified QEMU demonstrations**. Provide two runnable demonstrations of the same logical raw-network topology:

1. **External QEMU demo** — QEMU runs as a host process and is represented by GraphX as an externally managed node. This is the portable profile for macOS, Linux, and other hosts supported by the selected QEMU/guest artifacts.
2. **Containerized QEMU demo** — QEMU runs inside a Docker container and is represented by GraphX as a Docker-managed QEMU node. This profile is Linux-only and uses KVM when available, with an explicit TCG fallback for development and smoke testing.

Both demos must reuse the same guest image, guest application, host endpoint implementations, packet-observation pipeline, GUI behavior, topology semantics, capture/history formats, documentation structure, and acceptance vocabulary. The deployment boundary is the intentional difference.

This phase builds on the existing prototype under `examples/qemu-node` and the user experience of the standard GraphX demo implemented by root `compose.yaml`, `compose.history.yaml`, `scripts/demo.sh`, and `docs/complete-system-demo.md`. There is no canonical `examples/demo` directory; do not create or document one as though it exists.

## Objective

Deliver two understandable demonstrations of this logical topology:

```text
host-origin -- ordinary TCP and UDP --> qemu-node
qemu-node   -- ordinary TCP and UDP --> host-receiver
```

The QEMU guest sends and receives normal network traffic. Application packets are not GraphX envelopes and do not use GraphX length framing. GraphX provides topology, deployment description, orchestration, passive observation, bounded packet history, capture access, GUI display, and narrowly scoped control of the host-origin traffic generator.

The two demonstrations must look and behave alike from the user's perspective:

```text
demo.sh start [options]
demo.sh status
demo.sh verify
demo.sh logs
demo.sh token
demo.sh stop
```

## Working rules

- Treat the repository and current working tree as authoritative. Inspect `AGENTS.md`, repository status, the existing QEMU prototype, standard demo, configuration schema, telemetry service, GUI, tests, and documentation before editing.
- Preserve unrelated user changes. Do not reset, discard, overwrite, commit, push, publish, or deploy unless explicitly instructed.
- Refactor the existing QEMU prototype rather than copying divergent guest source, host peers, capture logic, or test helpers.
- Keep the logical topology and application payload contract identical between the two demos.
- Keep platform/deployment differences in thin launch, Compose, and configuration overlays.
- Do not label ordinary TCP/UDP packets as GraphX envelopes or reuse GraphX message history for packet history.
- Capture and history are enabled and bounded by default. Support explicit `--no-capture` and `--no-history` options.
- Do not require `privileged: true` for the default Linux container demo.
- Do not claim that a running QEMU process means the guest application is ready.
- Keep all waits, files, queues, payload previews, database growth, packet counts, log volume, shutdown periods, and retries bounded.
- Do not expose QMP, receiver ports, control endpoints, captures, or telemetry externally by default. Host-published services must bind to loopback unless the user explicitly opts into another address.
- Record design changes and unavoidable platform differences. Do not hide a platform limitation behind an unconditional success message.

## Required directory and reuse model

Evolve `examples/qemu-node` into one example suite with shared assets and two deployment profiles. A repository-compatible equivalent is acceptable, but the resulting structure must make duplication and ownership clear. Preferred layout:

```text
examples/qemu-node/
  README.md
  common/
    guest/
    buildroot-external/
    host/
    observer/
    scripts/
    tests/
  external/
    graphx.yaml
    compose.yaml
    compose.history.yaml
    scripts/demo.sh
    README.md
  container/
    graphx.yaml
    compose.yaml
    compose.history.yaml
    Dockerfile.qemu-runtime
    scripts/demo.sh
    README.md
```

Existing paths may remain as compatibility wrappers when removal would break documentation or tests. Wrappers must warn or forward deterministically and must not contain a second implementation.

## Shared topology and data-plane contract

Both configurations must describe the same logical nodes and four directed edges:

- `origin-qemu-udp`
- `origin-qemu-tcp`
- `qemu-receiver-udp`
- `qemu-receiver-tcp`

The graph must remain acyclic. `host-origin` and `host-receiver` are distinct real services, not fictitious aliases for one process.

All four edges carry raw application traffic:

```yaml
transport: udp # or tcp
framing: none
data_plane: external
```

If repository conventions justify different field names, document the decision and apply it consistently. The following invariants are mandatory:

1. `framing: none` is accepted only for explicitly raw/external data-plane edges.
2. Raw edges are validated, visualized, and observed but are never constructed by the GraphX transport factory.
3. Existing GraphX TCP and UDP edges continue to require their existing framing and behavior.
4. Existing configuration remains compatible.
5. The topology API distinguishes protocol, framing, observation source, deployment runtime, lifecycle ownership, and control capability.

## Shared guest and endpoint contract

Use exactly one x86_64 guest build and one guest application for both profiles. The guest application must:

- listen for ordinary TCP and UDP on a documented guest port;
- send ordinary TCP and UDP to a documented peer endpoint;
- use deterministic, inspectable test payloads with bounded sizes;
- expose a bounded readiness signal that proves the application, not merely the kernel, is ready;
- shut down or tolerate forced termination without corrupting host-side artifacts;
- require no GraphX library or GraphX wire protocol.

Use one implementation each for `host-origin` and `host-receiver`. Deployment-specific destinations are supplied through validated environment/configuration values, not source forks.

Preserve one guest networking contract where practical. For example:

- guest listens on TCP/UDP `8001`;
- QEMU exposes TCP/UDP `18001` to the environment containing QEMU;
- guest sends TCP/UDP to the slirp gateway `10.0.2.2:19001`;
- external profile publishes the receiver on host loopback `19001`;
- container profile provides a bounded TCP/UDP relay in the QEMU container namespace from `10.0.2.2:19001` to the `host-receiver` Compose service.

The exact ports may change to avoid current repository conflicts, but the same guest image must run unchanged in both profiles. Do not rely on a changing Docker container IP. Verify TCP and UDP separately; a TCP-only `guestfwd` solution is insufficient.

## Common build and artifact model

- Use the existing pinned Buildroot release and x86_64 guest definition unless an evidence-backed compatibility change is required.
- Build guest artifacts once and make both profiles consume the same versioned outputs.
- Separate a heavyweight guest builder image from the Linux QEMU runtime image.
- Keep toolchains and Buildroot sources out of the QEMU runtime image.
- Record guest artifact hashes and build metadata so the two profiles can prove they ran equivalent artifacts.
- Continue supporting the project's global organizational certificate and install-script mechanism in affected Docker builds without embedding private certificates in images or source control.
- Generated images, captures, databases, sockets, PID files, and logs must be ignored or placed under the established bounded output structure.

## Demo A — external QEMU profile

The external profile is the portable baseline.

### Deployment

- Run `host-origin`, `host-receiver`, packet observer, telemetry, and GUI as Docker services where Docker is available.
- Run QEMU on the host under the demo script's lifecycle control.
- Represent `qemu-node` with runtime `qemu`, execution `host`, and lifecycle `external` (or equivalent explicit metadata).
- Permit a mixed deployment in which Docker-managed services have images but the external QEMU node does not.
- On Docker Desktop use `host.docker.internal`; on native Linux external mode add or derive the supported host-gateway mapping.
- Bind QEMU forwarding, receiver publication, QMP, telemetry, and GUI only as broadly as documented. Default to loopback.

### Lifecycle

- Validate QEMU, Docker, free ports, artifacts, writable output paths, and supported host architecture before mutation.
- Start supporting containers, start QEMU, wait for the guest application, then start traffic.
- Use a private Unix QMP socket when supported; never publish QMP on an external TCP address.
- Track only the QEMU process owned by this run. Refuse unsafe stale PID reuse.
- On stop, request graceful guest/QEMU shutdown, wait for a bounded interval, then terminate only the verified owned process.
- Clean sockets and PID files idempotently while applying documented retention to captures and history.

### Platform behavior

- On macOS select HVF only when the host and guest combination supports it; otherwise use TCG and report the choice.
- On Linux external mode select KVM when accessible, otherwise TCG according to `--accel auto|kvm|tcg`.
- Never silently report hardware acceleration when QEMU actually used TCG.

## Demo B — Linux containerized QEMU profile

The container profile is Linux-only.

### QEMU runtime image

- Use a small Linux QEMU runtime image consuming the shared guest artifacts.
- Run QEMU as a supervised foreground process with correct signal propagation.
- Prefer a non-root runtime user. Grant only the group/device access required for `/dev/kvm`.
- Mount `/dev/kvm` only for KVM mode.
- Do not mount the Docker socket.
- Do not use `privileged: true` in the default profile.
- Do not require `/dev/net/tun` or `NET_ADMIN` for the default user-network profile.

### Compose deployment

- Run `host-origin`, `qemu-node`, `host-receiver`, observer, telemetry, and GUI as managed services.
- Keep data ports internal to the Compose network unless a verifier/debug option explicitly publishes them to loopback.
- Use service DNS rather than fixed container addresses.
- Mount the capture/output volume read-write only where necessary and read-only elsewhere.
- Determine the host KVM group safely and pass the required group membership without assuming one fixed GID.
- Provide `--accel auto|kvm|tcg`. `kvm` must fail clearly when unavailable; `auto` may fall back to TCG but must say so.

### Readiness and shutdown

- A container health check must prove that the guest application responds over both required protocol paths, not simply that QEMU has a PID.
- Use QMP or an equivalent private mechanism for orderly shutdown.
- Flush packet capture and history state before the service is considered stopped.
- Preserve useful QEMU/guest diagnostics when readiness fails.

## Passive observation, capture, and packet history

Use one common observation implementation for both profiles. It must consume a QEMU Ethernet capture stream/file without changing the guest application.

### Live observation

- Safely tail classic PCAP while QEMU is writing it, including partial global headers and partial packet records.
- Decode bounded Ethernet/IPv4/TCP/UDP metadata.
- Emit a distinct low-cardinality `network_packet` event or equivalent packet observation, not a fabricated GraphX message event.
- Attribute packets to topology edges using documented endpoint/direction rules and surface unknown/unattributed packets honestly.
- Publish live packet/byte/rate/error counters over the existing API/WebSocket path.
- Recover from QEMU restart, file replacement, truncation, and supported rotation.

### Capture

- Enable capture by default.
- Retain the source PCAP and produce valid Ethernet PCAPNG with link type 1.
- Catalog and download only validated, bounded capture files.
- Preserve path traversal, symlink, file-type, incomplete-file, and size protections.
- Do not apply the GraphX Lua dissector to raw traffic as though it contained GraphX envelopes.

### Packet history

- Enable packet history by default and store it separately from GraphX message history.
- Include timestamp, direction, edge attribution, protocol, addresses, ports, original/captured length, truncation, and a bounded payload preview or hash according to policy.
- Apply defaults aligned with the standard demo unless repository limits are stricter: one day, 50,000 records, and a 64 MiB database.
- Apply capture defaults aligned with the standard demo unless stricter: 100,000 packets and 64 MiB aggregate storage.
- Enforce age, record, packet, and byte bounds during normal operation and restart recovery.
- Treat malformed packets as bounded diagnostic records or drops; never crash or allocate from an untrusted length without validation.

## GUI and control requirements

Use the same GUI implementation for both demos.

### GUI

- Render the same application topology and raw TCP/UDP edges in both profiles.
- Render a network/deployment view that accurately differs:
  - external profile: Docker services plus host-managed QEMU VM;
  - container profile: Docker QEMU container containing a QEMU VM and guest application.
- Remove hard-coded claims that every node is a Docker container or every observed packet is GraphX-framed traffic.
- Show runtime, execution owner, accelerator, guest architecture, guest readiness, observation source, and control capability.
- Update counters over WebSocket without browser refresh.
- Keep Application, Network, History, and Capture navigation functional in every order, including returning from History.
- Clearly distinguish not started, booting, ready, degraded, stopped, and unavailable observation.

### Control

- Pause/resume controls only `host-origin` traffic generation.
- Reset clears live observer counters and documents whether it begins a new capture/history segment.
- Do not claim to pause or control the guest unless a separate guest-control feature is actually implemented and verified.
- Display the QEMU guest as not application-controllable in this phase.
- Use the existing generated control-token mechanism for mutations.
- Permit default loopback observation without a control token unless repository security policy explicitly requires one.

## CLI behavior and diagnostics

Both profile scripts must support the same verbs and common options. Profile-specific unsupported options must fail with help rather than being ignored.

`start` must print:

- selected profile and platform;
- selected accelerator and evidence source;
- guest artifact identity;
- capture/history enabled state and limits;
- GUI/API URLs;
- how to retrieve the control token;
- deterministic readiness outcome.

`verify` must run non-destructive profile-specific smoke checks and print individually attributable PASS/FAIL/SKIP results. `status` must distinguish service state, QEMU state, guest readiness, live traffic, capture, and history. `logs` must cover containers and, for external mode, host QEMU/guest logs. `stop` must be idempotent.

## Acceptance requirements

Use these identifiers unchanged in implementation tests, handoff, and verifier report:

- **QEMU-001 — Shared logical topology:** Both profiles expose the same valid acyclic three-node, four-edge TCP/UDP topology and endpoint semantics.
- **QEMU-002 — Raw protocol integrity:** Application traffic is ordinary TCP/UDP with `framing: none`; raw edges never enter the GraphX transport factory and existing GraphX transports remain unchanged.
- **QEMU-003 — Shared artifacts and code:** Both profiles use the same guest image, guest application, host endpoint implementations, observer, GUI, and test payload contract; artifact identity is verifiable.
- **QEMU-004 — External deployment model:** The portable profile models and operates QEMU as an externally managed host node while Docker services remain managed.
- **QEMU-005 — External runtime:** A supported macOS or Linux host boots the guest, proves bidirectional TCP and UDP, reports the actual accelerator, and stops cleanly.
- **QEMU-006 — Container deployment model:** The Linux profile models QEMU as a Docker-managed service and accurately exposes the nested container/VM runtime.
- **QEMU-007 — Container runtime and KVM:** Linux boots the guest in the QEMU container, proves bidirectional TCP and UDP, verifies KVM when selected, supports explicit TCG, and requires no privileged container in the default profile.
- **QEMU-008 — Lifecycle and readiness:** Both profiles provide bounded, idempotent startup, guest-application readiness, failure rollback, restart, and shutdown without unrelated process termination or orphaned resources.
- **QEMU-009 — Passive live observation:** Packet-derived node/edge counters update through the API and WebSocket without representing packets as GraphX messages.
- **QEMU-010 — Capture:** Capture is enabled by default, bounded, valid as Ethernet PCAP/PCAPNG, cataloged/downloadable, secure against unsafe paths, and disableable.
- **QEMU-011 — Packet history:** Separate packet history is enabled by default, queryable, persistent according to policy, bounded by time/count/bytes, and disableable.
- **QEMU-012 — GUI parity and accuracy:** Both demos use one GUI, render topology and deployment boundaries accurately, update live, and remain functional across all tab transitions.
- **QEMU-013 — Scoped control:** Authenticated pause/resume affects only host-origin, reset semantics are accurate, observation remains appropriately accessible, and unsupported guest control is not implied.
- **QEMU-014 — Unified CLI:** Both profiles implement the shared start/status/verify/logs/token/stop workflow with actionable diagnostics and deterministic exits.
- **QEMU-015 — Security and resource bounds:** Default exposure is loopback/private, QMP is private, no Docker socket or privileged container is required, and all storage, parsing, retries, waits, payloads, and diagnostics are bounded.
- **QEMU-016 — Compatibility and quality:** Existing builds, tests, packaging, standard demo, UDP examples, telemetry security, GUI, capture/history, documentation, and supported platform workflows remain compatible.
- **QEMU-017 — Documentation:** A common guide plus profile-specific guides explain prerequisites, architecture, operation, capture/history, GUI, controls, acceleration, limitations, cleanup, and troubleshooting without conflating inspection and runtime proof.

## Required tests

At minimum add automated tests for:

- shared topology equivalence with an explicit allowlist for deployment-only differences;
- raw-edge schema positive and adversarial negative cases;
- factory rejection/non-construction of raw edges;
- existing framed TCP/UDP compatibility;
- guest artifact identity across profiles;
- origin/receiver TCP and UDP behavior, reconnect, timeouts, bounded payloads, and deterministic exit;
- QEMU command construction and accelerator selection;
- stale PID/socket protection in external mode;
- signal propagation, health checks, and QMP shutdown in container mode;
- partial, truncated, replaced, rotated, malformed, and oversized PCAP input;
- packet attribution, counters, WebSocket updates, and reconnect;
- PCAP/PCAPNG link type, capture limits, download validation, traversal, and symlink rejection;
- packet-history age/count/byte bounds and restart recovery;
- GUI rendering for external and nested-container runtime models;
- History-to-Network/Application/Capture tab regression;
- control token, unauthorized mutation, pause/resume traffic cessation/restart, and reset semantics;
- repeated start/status/verify/stop and interrupted startup cleanup;
- Compose validation and image hardening;
- affected installation/package consumer and standard demo regressions.

Do not make timing tests depend on long arbitrary sleeps. Use bounded readiness polling and tolerant but meaningful deadlines.

## Implementation sequence

1. Audit and preserve the working tree; baseline existing tests and the QEMU prototype.
2. Record the shared topology, guest endpoint contract, deployment overlay strategy, raw-edge schema, and packet-observation boundary in an ADR.
3. Refactor shared guest, endpoint, build, and observer assets without changing behavior.
4. Add raw/external edge and runtime metadata support with compatibility tests.
5. Complete the external profile lifecycle and cross-platform host-gateway/accelerator handling.
6. Build the small QEMU runtime image and Linux Compose profile with KVM/TCG selection.
7. Implement the common passive observation, bounded capture, and separate packet-history pipeline.
8. Generalize the telemetry topology API and GUI for both runtime models.
9. Implement narrowly scoped controls and shared CLI behavior.
10. Add automated verification, documentation, and full regression evidence.

Do not implement the optional TAP/bridge, multicast/broadcast-through-guest, physical SDR, or arbitrary external-node orchestration extensions in this phase. Record them as later work.

## Verification before handoff

Run every feasible project check and distinguish actual execution from inspection:

- clean native configure/build and complete CTest;
- targeted configuration, raw-edge, observer, capture, history, telemetry, GUI, and lifecycle tests;
- formatting, static analysis, configured sanitizers, and fuzz targets;
- package installation and external consumer tests;
- standard demo and Phase 11 UDP regressions;
- Compose validation and all affected image builds;
- external-QEMU runtime on the available supported host;
- containerized QEMU TCG runtime on Linux;
- containerized QEMU KVM runtime on a KVM-capable Linux host;
- `tshark` validation of real PCAP and PCAPNG artifacts;
- browser/API/WebSocket live-update and tab-navigation checks.

A macOS result does not prove Linux container behavior. A TCG result does not prove KVM. A mounted `/dev/kvm` does not prove that QEMU activated KVM. A process/container state does not prove guest readiness. Do not report skipped checks as passing.

## Deliverables and handoff

Create `phase_12_handoff.md` in the repository with:

1. Outcome and scope summary.
2. QEMU-001 through QEMU-017 traceability matrix containing implementation evidence, test evidence, platform/profile, and status.
3. Shared architecture and explicit profile differences.
4. Raw data-plane, observation-plane, and control-plane separation.
5. Configuration/schema and compatibility decisions.
6. Files, public interfaces, images, volumes, ports, and commands added or changed.
7. Exact commands, results, durations, and environment information.
8. Runtime-verified, inspection-only, skipped, and unavailable checks.
9. Security, resource-bound, lifecycle, and failure-recovery assessment.
10. Known limitations and deferred TAP/L2/SDR work.
11. Risks and targeted areas for the independent verifier.

Phase 12 is complete only when both profiles share the intended implementation, all applicable acceptance requirements are satisfied, and unavailable platform runtime evidence is reported honestly.
